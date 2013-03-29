#!/usr/bin/env ruby
#
# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of 
# the GNU General Public License version 2.

require 'dl/import'

module Senna
  module API
    extend ::DL::Importable

    library_name = case RUBY_PLATFORM.downcase
      when /darwin/
        "libsenna.dylib"
      when /linux/, /freebsd|netbsd|openbsd|dragonfly/, /solaris/
        "libsenna.so"
      when /win32/
        "senna.dll"
    end

    if defined? SENNA_LIB_PATH
      library_name = File.join( SENNA_LIB_PATH, library_name )
    end

    dlload library_name

    typealias "sen_rc", "int"
    typealias "sen_encoding", "int"
    typealias "sen_index", "void"
    typealias "sen_records", "void"
    typealias "sen_values", "void"
    typealias "sen_recordh", "void"
    typealias "sen_rec_unit", "int"
    typealias "sen_sort_optarg", "void"
    typealias "sen_group_optarg", "void"
    typealias "sen_sym", "void"
    typealias "sen_sel_operator", "int"
    typealias "sen_query", "void"
    typealias "sen_set", "void"
    typealias "sen_id", "int"
    typealias "sen_snip", "void"
    typealias "sen_snip_mapping", "void"

    extern "sen_rc sen_init()"
    extern "sen_rc sen_fin()"
    extern "sen_index *sen_index_create(const char *, int, int, int, sen_encoding)"
    extern "sen_index *sen_index_open(const char *)"
    extern "sen_rc sen_index_close(sen_index *)"
    extern "sen_rc sen_index_remove(const char *)"
    extern "sen_rc sen_index_rename(const char *, const char *)"
    extern "sen_rc sen_index_upd(sen_index *, const void *, const char *, const char *)"
    extern "sen_records *sen_index_sel(sen_index *, const char *)"
    extern "int sen_records_next(sen_records *, void *, int, int *)"
    extern "sen_rc sen_records_rewind(sen_records *)"
    extern "int sen_records_curr_score(sen_records *)"
    extern "int sen_records_curr_key(sen_records *, void *, int)"
    extern "int sen_records_nhits(sen_records *)"
    extern "int sen_records_find(sen_records *, const void *)"
    extern "sen_rc sen_records_close(sen_records *)"
    extern "sen_values *sen_values_open()"
    extern "sen_rc sen_values_close(sen_values *)"
    extern "sen_rc sen_values_add(sen_values *, const char *, unsigned int)"
    extern "sen_records *sen_records_open(sen_rec_unit, sen_rec_unit, unsigned int)"
    extern "sen_records *sen_records_union(sen_records *, sen_records *)"
    extern "sen_records *sen_records_subtract(sen_records *, sen_records *)"
    extern "sen_records *sen_records_intersect(sen_records *, sen_records *)"
    extern "int sen_records_difference(sen_records *, sen_records *)"
    extern "sen_rc sen_records_sort(sen_records *, int, sen_sort_optarg *)"
    extern "sen_rc sen_records_group(sen_records *, int, sen_group_optarg *)"
    extern "const sen_recordh * sen_records_curr_rec(sen_records *)"
    extern "const sen_recordh * sen_records_at(sen_records *, const void *, unsigned int, unsigned int, int *, int *)"
    extern "sen_rc sen_record_info(sen_records *, const sen_recordh *, void *, int, int *, int *, int *, int *, int *)"
    extern "sen_rc sen_record_subrec_info(sen_records *, const sen_recordh *, int, void *, int, int *, int *, int *, int *)"
    extern "sen_index *sen_index_create_with_keys(const char *, sen_sym *, int, int, sen_encoding)"
    extern "sen_index *sen_index_open_with_keys(const char *, sen_sym *)"
    extern "sen_rc sen_index_update(sen_index *, const void *, unsigned int, sen_values *, sen_values *)"
    extern "sen_rc sen_index_select(sen_index *, const char *, sen_records *, sen_sel_operator, sen_select_optarg *)"
    extern "sen_rc sen_index_info(sen_index *, int *, int *, int *, sen_encoding *)"
#   extern "sen_set * sen_index_related_terms(sen_index *, const char *, void *, void *)"
    extern "sen_query *sen_query_open(const char *, sen_sel_operator, int, sen_encoding)"
    extern "const char *sen_query_rest(sen_query *)"
    extern "sen_rc sen_query_close(sen_query *)"
    extern "sen_rc sen_query_exec(sen_index *, sen_query *, sen_records *, sen_sel_operator)"
    extern "sen_rc sen_index_del(sen_index *, const void *)"
    extern "sen_set *sen_set_open(unsigned int, unsigned int, unsigned int)"
    extern "sen_rc sen_set_close(sen_set *)"
    extern "sen_rc sen_set_info(sen_set *, unsigned int *, unsigned int *, unsigned int*)"
    extern "sen_set_eh *sen_set_get(sen_set *, const void *, void **)"
    extern "sen_set_eh *sen_set_at(sen_set *, const void *, void **)"
    extern "sen_rc sen_set_del(sen_set *, sen_set_eh *)"
    extern "sen_set_cursor *sen_set_cursor_open(sen_set *)"
    extern "sen_set_eh *sen_set_cursor_next(sen_set_cursor *, void **, void **)"
    extern "sen_rc sen_set_cursor_close(sen_set_cursor *)"
    extern "sen_rc sen_set_element_info(sen_set *, const sen_set_eh *, void **, void **)"
    extern "sen_set *sen_set_union(sen_set *, sen_set *)"
    extern "sen_set *sen_set_subtract(sen_set *, sen_set *)"
    extern "sen_set *sen_set_intersect(sen_set *, sen_set *)"
    extern "int sen_set_difference(sen_set *, sen_set *)"
    extern "sen_set_eh *sen_set_sort(sen_set *, int, sen_set_sort_optarg *)"
    extern "sen_sym * sen_sym_create(const char *, unsigned int, unsigned int, sen_encoding)"
    extern "sen_sym * sen_sym_open(const char *)"
    extern "sen_rc sen_sym_info(sen_sym *, int *, unsigned int *, sen_encoding *, unsigned int*)"
    extern "sen_rc sen_sym_close(sen_sym *)"
    extern "sen_rc sen_sym_remove(const char *)"
    extern "sen_id sen_sym_get(sen_sym *, const unsigned char *)"
    extern "sen_id sen_sym_at(sen_sym *, const unsigned char *)"
    extern "sen_rc sen_sym_del(sen_sym *, const unsigned char *)"
    extern "unsigned int sen_sym_size(sen_sym *)"
    extern "int sen_sym_key(sen_sym *, sen_id, unsigned char *, int)"
    extern "sen_set * sen_sym_prefix_search(sen_sym *, const unsigned char *)"
    extern "sen_set * sen_sym_suffix_search(sen_sym *, const unsigned char *)"
    extern "sen_id sen_sym_common_prefix_search(sen_sym *, const unsigned char *)"
    extern "int sen_sym_pocket_get(sen_sym *, sen_id)"
    extern "sen_rc sen_sym_pocket_set(sen_sym *, sen_id, unsigned int)"
    extern "sen_id sen_sym_next(sen_sym *, sen_id)"
    extern "sen_snip *sen_snip_open(sen_encoding, int, int, int, const char *, const char *, sen_snip_mapping *)"
    extern "sen_rc sen_snip_close(sen_snip *)"
    extern "sen_rc sen_snip_add_cond(sen_snip *, const char *, const char *, const char *)"
    extern "sen_rc sen_snip_exec(sen_snip *, const char *, int *, int *)"
    extern "sen_rc sen_snip_get_result(sen_snip *, int, char *)"
  end

  def self.records_next(r)
    API.sen_records_next(r, DL::PtrData.new(0), 0, DL::PtrData.new(0))
  end

  def self.records_curr_key(r)
    size = API.sen_records_curr_key(r, DL::PtrData.new(0), 0)
    ptr = DL.malloc(size)
    API.sen_records_curr_key(r, ptr, size)
    return ptr.to_s
  end

  def self.api_delegate(name)
    sclass = (class << Senna; self; end)
    sclass.class_eval {
      define_method(name) { |*args| API.send( "sen_#{name}", *args ) }
    }
  end

  api_delegate :index_create
  api_delegate :index_open
  api_delegate :index_close
  api_delegate :index_remove
  api_delegate :index_rename
  api_delegate :index_upd
  api_delegate :index_sel
  api_delegate :records_rewind
  api_delegate :records_curr_score
  api_delegate :records_nhits
  api_delegate :records_find
  api_delegate :records_close
  api_delegate :values_open
  api_delegate :values_close
  api_delegate :values_add
  api_delegate :records_open
  api_delegate :records_union
  api_delegate :records_subtract
  api_delegate :records_intersect
  api_delegate :records_difference
  api_delegate :records_sort
  api_delegate :records_group
  api_delegate :records_curr_rec
  api_delegate :records_at
  api_delegate :record_info
  api_delegate :record_subrec_info
  api_delegate :index_create_with_keys
  api_delegate :index_open_with_keys
  api_delegate :index_update
  api_delegate :index_select
  api_delegate :index_info
  api_delegate :index_related_terms
  api_delegate :query_open
  api_delegate :query_rest
  api_delegate :query_close
  api_delegate :query_exec
  api_delegate :index_del
  api_delegate :set_open
  api_delegate :set_close
  api_delegate :set_info
  api_delegate :set_get
  api_delegate :set_at
  api_delegate :set_del
  api_delegate :set_cursor_open
  api_delegate :set_cursor_next
  api_delegate :set_cursor_close
  api_delegate :set_element_info
  api_delegate :set_union
  api_delegate :set_subtract
  api_delegate :set_intersect
  api_delegate :set_difference
  api_delegate :set_sort
  api_delegate :sym_create
  api_delegate :sym_open
  api_delegate :sym_info
  api_delegate :sym_close
  api_delegate :sym_remove
  api_delegate :sym_get
  api_delegate :sym_at
  api_delegate :sym_del
  api_delegate :sym_size
  api_delegate :sym_key
  api_delegate :sym_prefix_search
  api_delegate :sym_suffix_search
  api_delegate :sym_common_prefix_search
  api_delegate :sym_pocket_get
  api_delegate :sym_pocket_set
  api_delegate :sym_next
  api_delegate :snip_open
  api_delegate :snip_close
  api_delegate :snip_add_cond
  api_delegate :snip_exec
  api_delegate :snip_get_result

  API.sen_init
end


if $0 == __FILE__
  i = Senna::index_create('/tmp/hoge', 0, 1, 0, 0)
  Senna::index_upd(i, 'abc', nil, 'def ghi')
  r = Senna::index_sel(i, 'def')
  Senna::records_next(r)
  puts Senna::records_curr_key(r)
end
