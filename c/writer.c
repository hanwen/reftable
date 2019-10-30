#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <search.h>

#include "record.h"
#include "block.h"
#include "api.h"
#include "writer.h"

typedef struct _writer {
  int (*write)(void*, byte *, int);
  void *write_arg;
  int pending_padding;

  slice last_key;

  uint64 next;
  write_options opts;

  byte *block ;
  block_writer *block_writer;
  index_record *index;
  int index_len;
  int index_cap;

  // tree for use with tsearch
  void *obj_index_tree;

  stats stats;
} writer;

int padded_write(writer*w, slice out, int padding )  {
  if (w->pending_padding > 0) {
    byte *zeroed = calloc(w->pending_padding, 1);
    int n  = w->write(w->write_arg, zeroed, w->pending_padding);
    if (n < 0) {
      return n;
    }

    w->pending_padding = 0;
    free(zeroed);
  }

  w->pending_padding = padding;
  return w->write(w->write_arg, out.buf, out.len);
}


void options_set_defaults(write_options *opts) {
  if (opts->restart_interval == 0) {
    opts->restart_interval = 16;
  }

  if (opts->block_size == 0) {
    opts->block_size = 16;
  }
}


int writer_write_header(writer *w, byte* dest) {
  strcpy((char*)dest, "REFT");
  put_u24(dest + 5, w->opts.block_size);
  put_u64(dest + 8, w->opts.min_update_index);
  put_u64(dest + 16, w->opts.max_update_index);
  return 24;
}


block_writer* writer_new_block_writer(writer* w, byte typ) {
  int block_start =0 ;
  if (w->next == 0) {
    block_start = writer_write_header(w, w->block);
  }

  block_writer *bw = new_block_writer(typ, w->block, w->opts.block_size, block_start);
  bw->restart_interval = w->opts.restart_interval;
  return bw;
}

writer *new_writer(int (*writer_func)(void*, byte*, int), void* writer_arg, write_options *opts) {
  options_set_defaults(opts);
  if (opts->block_size >= (1<<24)) {
    abort();
  }
  writer *wp = calloc(sizeof(writer),1);
  wp->block = calloc(opts->block_size, 1);
  wp->write = writer_func;
  wp->write_arg = writer_arg;
  wp->opts = *opts;
  wp->block_writer = writer_new_block_writer(wp, BLOCK_TYPE_REF);
  
  return wp;
}

void writer_free(writer*w) {
  free(w->block);
  free(w);
}

typedef struct {
  slice hash;
  uint64 *offsets;
  int offset_len;
  int offset_cap;
} obj_index_tree_node;

int obj_index_tree_node_compare(const void *a, const void *b) {
  return slice_compare(((const obj_index_tree_node*)a)->hash,
    ((const obj_index_tree_node*)b)->hash);
}

void writer_index_hash(writer *w, slice hash) {
  if (!w->opts.index_objects) {
    return;
  }

  uint64 off = w->next;

  obj_index_tree_node want= {};
  slice_copy(&want.hash, hash);

  obj_index_tree_node *node = tfind(&want, w->obj_index_tree, &obj_index_tree_node_compare);
  if (node == NULL) {
    node = calloc(sizeof(obj_index_tree_node), 1);
    slice_copy(&node->hash, hash);
    tsearch(node, w->obj_index_tree, &obj_index_tree_node_compare);
  }

  if (node->offset_len > 0 && node->offsets[node->offset_len-1] == off) {
    return;
  }
      
  if (node->offset_len == node->offset_cap) {
    node->offset_cap = 2*node->offset_cap + 1;
    node->offsets = realloc(node->offsets, node->offset_cap);
  }

  node->offsets[node->offset_len++] = off;
  slice_free(&want.hash);
}


int writer_add_record(writer*w, record*rec) {
  int result  = -1;
  slice key = {};
  rec->ops->key(rec, &key);
  if (slice_compare(w->last_key, key) >= 0) {
    goto exit;
  }

  slice_copy(&w->last_key, key);

  if (w->block_writer == NULL) {
    w->block_writer = writer_new_block_writer(w, rec->ops->type());
  }

  assert(block_writer_type(w->block_writer) == rec->ops->type());

  if (block_writer_add(w->block_writer, rec) == 0) {
    result =  0;
    goto exit;
  }

  int err = writer_flush_block(w);
  if (err < 0) {
    result = err;
    goto exit;
  }

  err = block_writer_add(w->block_writer, rec);
  if (err < 0) {
    // XXX error code.
    result = err;
    goto exit;
  }

  result = 0;
 exit:
  slice_free(&key);
  return result;
}

int writer_add_ref(writer * w, ref_record* ref) {
  if (ref->update_index < w->opts.min_update_index ||
      ref->update_index > w->opts.max_update_index){
    return -1;
  }

  ref->update_index -= w->opts.min_update_index;
  int err = writer_add_record(w, (record*)ref);
  if (err < 0) {
    return err;
  }

  if (ref->value != NULL) {
    slice h = {
	       .buf = ref->value,
	       .len = HASH_SIZE,
    };
	       
    writer_index_hash(w, h);
  }
  if (ref->target_value != NULL) {
    slice h = {
	       .buf = ref->target_value,
	       .len = HASH_SIZE,
    };
    writer_index_hash(w, h);
  }
  return 0;
}

int writer_finish_public_section(w) {
  // XXX TODO
  return 0;
}

int writer_close(writer *w) {
  writer_finish_public_section(w);

  byte footer[68];
  byte *p = footer;
  writer_write_header(w, footer);
  p += 24;
  put_u64(p, w->stats->ref_stats->index_offset);
  p += 8;
  put_u64(p, (w->stats->obj_stats->offset) << 5 | w->stat->object_id_len);
  p += 8;
  put_u64(p, w->stats->obj_stats->index_offset);
  p += 8;
  put_u64(p, 0);
  p += 8;
  put_u64(p, 0);
  p += 8;

  // XXX compute CRC-32.
  put_u32(p, 0);
  p += 4;
  w->padded_writer->pending_padding = 0;

  int n = padded_write(&w->padded_writer, footer, sizeof(footer));
  if (n < 0) {
    return n;
  }

  assert(n == sizeof(footer));

  return 0;
}

const int debug = 0;

int writer_flush_block(writer *w) {
  if (w->block_writer == NULL) {
    return 0;
  }
  if (w->block_writer->entries == 0) {
    return 0;
  }

  byte typ  = block_writer_type(w->block_writer);

  block_stats *bstats =NULL;
  switch (typ) {
  case 'r':
    bstats =  &w->stats->ref_stats;
  case 'o':
    bstats =  &w->stats->obj_stats;
  case 'i':
    bstats =  &w->stats->idx_stats;
  }

  if (bstats->blocks == 0) {
    bstats->offset = w->next;
  }

  int raw_bytes = block_writer_finish(w->block_writer);
  if (raw_bytes < 0){
    return raw_bytes;
  }

  int padding = w->opts->block_size - raw_bytes;
  if (w->opts->unpadded || typ == BLOCK_TYPE_LOG) {
    padding = 0;
  }

  bstats->entries += w->block_writer->entries;
  bstats->restarts += w->block_writer->restarts;
  bstats->blocks++;
  w->stats->blocks++;

  if (debug) {
    fprintf(stderr, "block %c off %d sz %d (%d)",
			typ, w->next, raw_bytes, getU24(w->block + w->block_writer->header_off+1))
  }

  slice out = {
	       .buf = w->block,
	       .len = raw_bytes,
  };
  int n = padded_write(w->padded_writer, out, padding);
  if (n < 0) {
    return n;
  }

  if (w->index_cap == w->index_len) {
    w->index_cap = 2*w->index_cap + 1;
    w->index = realloc(w->index, w->index_cap);
  }
  w->index[w->index_len] = w->next;
  free(w->block_writer);
  w->block_writer = NULL;
  return 0;
}