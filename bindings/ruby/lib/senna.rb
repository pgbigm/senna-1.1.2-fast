#!/usr/bin/env ruby
#
# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.

require 'senna_api'
# todo: load 'senna/senna_api_dl' if 'senna_api' is unavailable

module Senna
  INDEX_NORMALIZE               =      0x0001
  INDEX_SPLIT_ALPHA             =      0x0002
  INDEX_SPLIT_DIGIT             =      0x0004
  INDEX_SPLIT_SYMBOL            =      0x0008
  INDEX_MORPH_ANALYSE           =      0x0000
  INDEX_NGRAM                   =      0x0010
  INDEX_DELIMITED               =      0x0020
  INDEX_ENABLE_SUFFIX_SEARCH    =      0x0100
  INDEX_DISABLE_SUFFIX_SEARCH   =      0x0200
  INDEX_WITH_VACUUM             =      0x8000

  SYM_MAX_KEY_SIZE              =      0x2000
  SYM_WITH_SIS                  =  0x80000000

  SNIP_NORMALIZE                =      0x0001
  SNIP_COPY_TAG                 =      0x0002
  SNIP_SKIP_LEADING_SPACES      =      0x0004
  SNIP_MAP_HTMLENCODE           =          -1

  RC_SUCCESS                    =      0x0000
  RC_MEMORY_EXHAUSTED           =      0x0001
  RC_INVALID_FORMAT             =      0x0002
  RC_FILE_OPERATION_ERROR       =      0x0003
  RC_INVALID_ARGUMENT           =      0x0004
  RC_OTHER_ERROR                =      0x0005

  ENC_DEFAULT                   =      0x0000
  ENC_NONE                      =      0x0001
  ENC_EUC_JP                    =      0x0002
  ENC_UTF8                      =      0x0003
  ENC_SJIS                      =      0x0004

  REC_DOCUMENT                  =      0x0000
  REC_SECTION                   =      0x0001
  REC_POSITION                  =      0x0002
  REC_USERDEF                   =      0x0003
  REC_NONE                      =      0x0004

  SEL_OR                        =      0x0000
  SEL_AND                       =      0x0001
  SEL_BUT                       =      0x0002
  SEL_ADJUST                    =      0x0003
  SEL_OPERATOR                  =      0x0004

  SEL_EXACT                     =      0x0000
  SEL_PARTIAL                   =      0x0001
  SEL_UNSPLIT                   =      0x0002
  SEL_NEAR                      =      0x0003
  SEL_NEAR2                     =      0x0004
  SEL_SIMILAR                   =      0x0005
  SEL_TERM_EXTRACT              =      0x0006
  SEL_PREFIX                    =      0x0007
  SEL_SUFFIX                    =      0x0008

  SORT_DESCENDING               =      0x0000
  SORT_ASCENDING                =      0x0001

  LOG_NONE                      =      0x0000
  LOG_EMERG                     =      0x0001
  LOG_ALERT                     =      0x0002
  LOG_CRIT                      =      0x0003
  LOG_ERROR                     =      0x0004
  LOG_WARNING                   =      0x0005
  LOG_NOTICE                    =      0x0006
  LOG_INFO                      =      0x0007
  LOG_DEBUG                     =      0x0008
  LOG_DUMP                      =      0x0009

  LOG_TIME                      =      0x0001
  LOG_TITLE                     =      0x0002
  LOG_MESSAGE                   =      0x0004
  LOG_LOCATION                  =      0x0008

  DEFAULT_PORT                  =      10041

  CTX_MORE                      =      0x0001
  CTX_TAIL                      =      0x0002
  CTX_HEAD                      =      0x0004
  CTX_QUIET                     =      0x0008
  CTX_QUIT                      =      0x0010

  STR_REMOVEBLANK               =      0x0001
  STR_WITH_CTYPES               =      0x0002
  STR_WITH_CHECKS               =      0x0004

  class Index
    attr_reader :handle
    attr_reader :key_size, :flags, :initial_n_segments, :encoding
    attr_reader :nrecords_keys, :file_size_keys
    attr_reader :nrecords_lexicon, :file_size_lexicon
    attr_reader :inv_seg_size, :inv_chunk_size

    def Index.callback(handle)
      proc { Senna::sen_index_close(handle) }
    end

    def Index::create(path, key_size=0, flags=0, initial_n_segments=0, encoding=ENC_DEFAULT)
      handle = Senna::sen_index_create(path, key_size, flags, initial_n_segments, encoding)
      return new(handle)
    end

    def Index::open(path)
      handle = Senna::sen_index_open(path)
      return new(handle)
    end

    def initialize(handle)
      @handle = handle
      ObjectSpace.define_finalizer(self, Index.callback(handle))
    end

    def upd(id, oldvalue, newvalue)
      id = [id].pack('L') if Fixnum === id
      oldvalue ||= ''
      newvalue ||= ''
      Senna::sen_index_upd(@handle, id, oldvalue, oldvalue.length,
                           newvalue, newvalue.length)
    end

    def update(id, section, oldvalue, newvalue)
      # section must be >= 1
      id = [id].pack('L') if Fixnum === id
      oldvalue = Values::open(oldvalue) if String === oldvalue
      newvalue = Values::open(newvalue) if String === newvalue
      oldvalue ||= Values::open
      newvalue ||= Values::open
      Senna::sen_index_update(@handle, id, section, oldvalue.handle, newvalue.handle)
    end

    def sel(query)
      r = Senna::sen_index_sel(@handle, query, query.length)
      return Records.new(r) if r
    end

    def select(query, records=nil, op=SEL_OR, optarg=nil)
      records ||= Records.new
      Senna::sen_index_select(@handle, query, query.length, records.handle, op, optarg)
      return records
    end

    def query(query, default_op=SEL_OR, max_exprs=32, encoding=ENC_DEFAULT)
      return unless String === query
      q = Senna::sen_query_open(query, query.length, default_op, max_exprs, encoding)
      r = Senna::sen_records_open(REC_DOCUMENT, REC_NONE, 0)
      Senna::sen_query_exec(@handle, q, r, SEL_OR)
      Senna::sen_query_close(q)
      return Records.new(r)
    end

    def keys
      Senna::Sym.new(Senna::sen_index_keys(@handle))
    end

    def lexicon
      Senna::Sym.new(Senna::sen_index_lexicon(@handle))
    end

    def inv
      Senna::sen_index_inv(@handle)
    end

    def close
      rc = Senna::sen_index_close(@handle)
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
      return rc
    end

    def path
      size, = Senna::sen_index_path(@handle, 1)
      _,res = Senna::sen_index_path(@handle, size)
      return res
    end

    def remove
      _path = path
      self.close
      Senna::sen_index_remove(_path)
    end

    def rename(newpath)
      _path = path
      Senna::sen_index_close(@handle)
      Senna::sen_index_rename(_path, newpath)
      @handle = Senna::sen_index_open(newpath)
    end

    def inv_check(term)
      e = InvEntry.new(self, term)

      printf("key:            %10s\n", e.key)
      printf("term_id:        %10d\n", e.term_id)

      case e.info_rc
      when 0
        puts('no entry in array')
      when 1
        puts('entry is void')
      when 2
        printf("entry:    %10d\n", e.entry)
      when 3
        printf("entry:    %10d\n", e.entry)
        puts('no buffer in inv')
      when 4
        printf("entry:          %10d\n", e.entry)
        printf("pocket:         %10d\n", e.pocket)
        printf("chunk:          %10d\n", e.chunk)
        printf("chunk_size:     %10d\n", e.chunk_size)
        printf("buffer_free:    %10d\n", e.buffer_free)
        printf("nterms:         %10d\n", e.nterms)
        printf("nterms_void:    %10d\n", e.nterms_void)
        printf("tid_in_buffer:  %10d\n", e.tid_in_buffer)
        printf("size_in_chunk:  %10d\n", e.size_in_chunk)
        printf("pos_in_chunk:   %10d\n", e.pos_in_chunk)
        printf("size_in_buffer: %10d\n", e.size_in_buffer)
        printf("pos_in_buffer:  %10d\n", e.pos_in_buffer)
      end
    end

    def key_check(key)
      case key
      when String
        str = key
        rid = keys.at(key)
      when Fixnum
        _,str = keys.key(key)
        rid = key
      else
        return
      end
      pocket = keys.pocket(rid)
      printf("str:            %10s\n", str)
      printf("rid:            %10d\n", rid)
      printf("pocket:         %10d\n", pocket)
    end

    def info
      info = Senna::sen_index_info(@handle)
      @key_size, @flags, @initial_n_segments, @encoding,
        @nrecords_keys, @file_size_keys,
        @nrecords_lexicon, @file_size_lexicon,
        @inv_seg_size, @inv_chunk_size = info
      return info
    end

    def query_exec(query, records=nil, op=SEL_OR)
      records ||= Records.new
      Senna::sen_query_exec(@handle, query.handle, records.handle, op)
      return records
    end
  end

  class InvEntry
    attr_reader :index, :key, :term_id
    attr_reader :info_rc, :entry, :pocket, :chunk, :chunk_size, :buffer_free
    attr_reader :nterms, :nterms_void, :tid_in_buffer, :size_in_chunk
    attr_reader :pos_in_chunk, :size_in_buffer, :pos_in_buffer
    def initialize(index, term)
      @index = index
      case term
      when String
        @key = term
        @term_id = index.lexicon.at(term)
      when Fixnum
        _, @key = index.lexicon.key(term)
        @term_id = term
      else
        return
      end
      info = Senna::sen_inv_entry_info(index.inv, @term_id)
      @info_rc, @entry, @pocket, @chunk, @chunk_size, @buffer_free,
        @nterms, @nterms_void, @tid_in_buffer, @size_in_chunk,
        @pos_in_chunk, @size_in_buffer, @pos_in_buffer =
        Senna::sen_inv_entry_info(index.inv, term_id)
    end
  end

  class Sym
    attr_reader :handle, :path
    def Sym.callback(handle)
      proc { Senna::sen_sym_close(handle) }
    end

    def Sym::create(path, key_size=0, flags=0, encoding=0)
      handle = Senna::sen_sym_create(path, key_size, flags, encoding)
      return new(handle, path)
    end

    def index_create_with_keys(path, flags=0, initial_n_segments=0, encoding=0)
      handle = Senna::sen_index_create_with_keys(path, @handle, flags, initial_n_segments, encoding)
      return Senna::Index.new(handle)
    end

    def index_open_with_keys(path)
      handle = Senna::sen_index_open_with_keys(path, @handle)
      return Senna::Index.new(handle)
    end

    def Sym::open(path)
      handle = Senna::sen_sym_open(path)
      return new(handle, path)
    end

    def initialize(handle, path=nil)
      @handle = handle
      @path = path
      ObjectSpace.define_finalizer(self, Sym.callback(@handle)) if path
    end

    def close
      return unless @path
      rc = Senna::sen_sym_close(@handle)
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
      return rc
    end

    def remove
      return unless @path
      self.close
      Senna::sen_sym_remove(@path)
    end

    def info
      inf = Senna::sen_sym_info(@handle)
      return {'key_size' => inf[1], 'flags' => inf[2], 'encoding' => inf[3], 'nrecords' => inf[4], 'file_size' => inf[5]}
    end

    def get(key)
      Senna::sen_sym_get(@handle, key)
    end

    def at(key)
      Senna::sen_sym_at(@handle, key)
    end

    def del(key)
      Senna::sen_sym_del(@handle, key)
    end

    def key(id)
      Senna::sen_sym_key(@handle, id, SYM_MAX_KEY_SIZE)
    end

    def pocket_get(id)
      Senna::sen_sym_pocket_get(@handle, id)
    end

    def pocket_set(id, value)
      Senna::sen_sym_pocket_set(@handle, id, value)
    end

    def size
      Senna::sen_sym_size(@handle)
    end

    def next(id)
      Senna::sen_sym_next(@handle, id)
    end

    def terms2array(terms)
      return nil unless terms
      res = []
      c = Senna::sen_set_cursor_open(terms)
      loop {
        eh,id,_ = Senna::sen_set_cursor_next(c, 4, -1)
        break unless eh
        _,str = Senna::sen_sym_key(@handle, id.unpack('L')[0], SYM_MAX_KEY_SIZE)
        res << str
      }
      Senna::sen_set_cursor_close(c)
      res
    end

    def prefix_search(key)
      terms2array(Senna::sen_sym_prefix_search(@handle, key))
    end

    def suffix_search(key)
      terms2array(Senna::sen_sym_suffix_search(@handle, key))
    end

    def common_prefix_search(key)
      id = Senna::sen_sym_common_prefix_search(@handle, key)
      _,str = Senna::sen_sym_key(@handle, id, SYM_MAX_KEY_SIZE)
      return str
    end
  end

  class Element
    attr_reader :phandle, :handle, :key, :value
    def initialize(phandle, handle)
      @phandle = phandle
      @handle = handle
      @key, @value = Senna::sen_record_info(@phandle, @handle)
    end
  end

  class Set
    attr_reader :handle
    def Set.callback(handle)
      proc { Senna::sen_set_close(handle) }
    end

    def Set::open(keysize, value_size, index_size)
      handle = Senna::sen_set_open(keysize, value_size, index_size);
      return new(handle)
    end

    def initialize(handle, closable=true)
      @handle = handle
      @closable = closable
      ObjectSpace.define_finalizer(self, Set.callback(handle)) if closable
    end

    def close
      return unless @closable
      rc = Senna::sen_set_close(@handle)
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
      return rc
    end

    def info
      inf = Senna::sen_set_info(@handle)
      return { 'key_size' => inf[0], 'value_size' => inf[1], 'n_entries' => inf[2] }
    end

    def []=(key, value)
      return get(key, value)
    end

    def get(key, value)
      eh, _ = Senna::sen_set_get(@handle, key, value)
      return eh
    end

    def [](key)
      _, value = Senna::sen_set_at(@handle, key)
      return value
    end

    def at(key)
      eh, _ = Senna::sen_set_at(@handle, key)
      return eh
    end

    def del(eh)
      Senna::sen_set_del(@handle, eh)
    end

    def cursor_open
      Senna::sen_set_cursor_open(@handle)
    end

    def cursor_next(cursor)
      eh, _, _ = Senna::sen_set_cursor_next(cursor)
      return eh
    end

    def cursor_close(cursor)
      Senna::sen_set_cursor_close(cursor)
    end

    def element_info(eh, key=0, value=0)
      Senna::sen_set_element_info(@handle, eh, key, value)
    end

    def +(obj)
      union(obj)
    end

    def union(obj)
      Senna::sen_set_union(@handle, obj)
    end

    def -(obj)
      subtract(obj)
    end

    def subtract(obj)
      Senna::sen_set_subtract(@handle, obj)
    end

    def *(obj)
      intersect(obj)
    end

    def intersect(obj)
      Senna::sen_set_intersect(@handle, obj)
    end

    def difference(obj)
      Senna::sen_set_difference(@handle, obj)
    end

    def sort(limit=0, optarg=nil)
      Senna::sen_set_sort(@handle, limit, optarg)
    end
  end

  class Records
    attr_reader :handle
    def Records.callback(handle)
      proc { Senna::sen_records_close(handle) }
    end

    def Records::open(record_unit=REC_DOCUMENT, subrec_unit=REC_NONE, max_n_subrecs=0)
      handle = Senna::sen_records_open(record_unit, subrec_unit, max_n_subrecs)
      return new(handle)
    end

    def initialize(handle=nil, closable=true)
      handle ||= Senna::sen_records_open(REC_DOCUMENT, REC_NONE, 0)
      @handle = handle
      @closable = closable
      ObjectSpace.define_finalizer(self, Records.callback(handle)) if closable
    end

    def nhits
      Senna::sen_records_nhits(@handle)
    end

    def each
      while rec = self.next do
        yield self.curr_key, self.curr_score
      end
    end

    def find(key)
      key = [key].pack('L') if Fixnum === key
      Senna::sen_records_find(@handle, key)
    end

    def rewind
      Senna::sen_records_rewind(@handle)
    end

    def next
      size,_,score = Senna::sen_records_next(@handle, 0)
      if size > 0
        _, res = Senna::sen_records_curr_key(@handle, size)
      else
        res = nil
      end
      return res
    end

    def curr_rec(bufsize=0)
      rh = Senna::sen_records_curr_rec(@handle)
      return Record.new(@handle, rh, bufsize)
    end

    def at(key, section, pos, bufsize=0)
      rh, _, _ = Senna::sen_records_at(@handle, key, section, pos)
      return Record.new(@handle, rh, bufsize)
    end

    def curr_key
      size, res = Senna::sen_records_curr_key(@handle, 128)
      return res if size <= 128
      _, res = Senna::sen_records_curr_key(@handle, size)
      return res
    end

    def curr_score
      return Senna::sen_records_curr_score(@handle)
    end

    def sort(limit, opt=nil)
      return Senna::sen_records_sort(@handle, limit, opt)
    end

    def group(limit, opt=nil)
      return Senna::sen_records_group(@handle, limit, opt)
    end

    def close
      return unless @closable
      rc = Senna::sen_records_close(@handle)
      @handle = nil
      self.invalidation
      return rc
    end

    def invalidation
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
    end

    def records_restruct(subj, newhandle)
      self.invalidation
      subj.invalidation
      @handle = newhandle
      ObjectSpace.define_finalizer(self, Records.callback(handle))
    end

    def union(subj)
      newhandle = Senna::sen_records_union(@handle, subj.handle)
      self.records_restruct(subj, newhandle)
    end

    def subtract(subj)
      newhandle = Senna::sen_records_subtract(@handle, subj.handle)
      self.records_restruct(subj, newhandle)
    end

    def intersect(subj)
      newhandle = Senna::sen_records_intersect(@handle, subj.handle)
      self.records_restruct(subj, newhandle)
    end

    def difference(subj)
      Senna::sen_records_difference(@handle, subj.handle)
    end

    def sort(limit, optarg=nil)
      Senna::sen_records_sort(@handle, limit, optarg)
    end

    def group(limit, optarg=nil)
      Senna::sen_records_group(@handle, limit, optarg)
    end
  end

  class Record
    attr_reader :phandle, :handle, :key, :keysize, :section, :pos, :score, :n_subrecs
    def initialize(phandle, handle, bufsize=0)
      @phandle = phandle
      @handle = handle
      rc, @key, @keysize, @section, @pos, @score, @n_subrecs =
        Senna::sen_record_info(@phandle, @handle, bufsize)
    end

    def subrec(index, bufsize=0)
      return SubRecord.new(@phandle, @handle, index, bufsize)
    end
  end

  class SubRecord
    attr_reader :phandle, :handle, :index, :key, :keysize, :section, :pos, :score
    def initialize(phandle, handle, index, bufsize=0)
      @phandle = phandle
      @handle = handle
      @index = index
      rc, @key, @keysize, @section, @pos, @score =
        Senna::sen_record_subrec_info(@phandle, @handle, index, bufsize)
    end
  end

  class Values
    attr_reader :handle
    def Values.callback(handle)
      proc { Senna::sen_values_close(handle) }
    end

    def Values::open(str=nil, weight=0)
      handle = Senna::sen_values_open
      ret = new(handle)
      ret.add(str, weight) unless str.nil?
      return ret
    end

    def add(str, weight=0)
      Senna::sen_values_add(@handle, str, str.length, weight)
    end

    def initialize(handle)
      @handle = handle
      ObjectSpace.define_finalizer(self, Values.callback(@handle))
    end

    def close
      rc = Senna::sen_values_close(@handle)
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
      return rc
    end
  end

  class Query
    attr_reader :handle
    def Query.callback(handle)
      proc { Senna::sen_query_close(handle) }
    end

    def Query::open(str, default_op=SEL_OR, max_exprs=32, encoding=ENC_DEFAULT)
      handle = Senna::sen_query_open(str, str.length, default_op, max_exprs, encoding)
      return new(handle)
    end

    def initialize(handle)
      @handle = handle
      ObjectSpace.define_finalizer(self, Query.callback(@handle))
    end

    def close
      rc = Senna::sen_query_close(@handle)
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
      return rc
    end

    def rest
      len, _ = Senna::sen_query_rest(@handle, 0)
      return Senna::sen_query_rest(@handle, len)[1]
    end

    def exec(index, records=nil, op=SEL_OR)
      records ||= Records.new
      Senna::sen_query_exec(index.handle, @handle, records.handle, op)
      return records
    end

    def term
      # TODO: implement!
      raise NameError, 'Query#term is not implemented'
    end

    def scan(strs, flags)
      # TODO: implement!
      # [found, score]
      raise NameError, 'Query#scan is not implemented'
    end

    def snip(flags, width, max_results, tags)
      # TODO: implement!
      # Snip.new(handle)
      raise NameError, 'Query#snip is not implemented'
    end
  end

  class Snip
    def Snip.callback(handle)
      proc { Senna::sen_snip_close(handle) }
    end

    def Snip::open(conds=nil, width=100, max_results=3, opentag='', closetag='', map=nil, enc=nil, flags=SNIP_NORMALIZE)
      enc ||= [nil, 'NONE', 'EUC', 'UTF8', 'SJIS'].index($KCODE) || 0
      case conds
      when Array
        condkeys = conds
        condvals = Hash.new([nil, nil])
      when Hash
        condkeys = conds.keys
        condvals = conds
      when String
        condkeys = [conds]
        condvals = { conds => [nil, nil] }
      else
        condkeys = []
      end
      handle = Senna::sen_snip_open(enc, flags, width, max_results,
                                    opentag, opentag.length,
                                    closetag, closetag.length,
                                    map)
      condkeys.each {|c|
        opentag_len = condvals[c][0].nil? ? 0 : condvals[c][0].length
        closetag_len = condvals[c][1].nil? ? 0 : condvals[c][1].length
        Senna::sen_snip_add_cond(handle, c, c.length,
                                 condvals[c][0], opentag_len,
                                 condvals[c][1], closetag_len)
      }
      return Snip.new(handle)
    end

    def initialize(handle)
      @handle = handle
      ObjectSpace.define_finalizer(self, Snip.callback(@handle))
    end

    def add(cond, opentag=nil, closetag=nil)
      opentag_len = opentag.nil? ? 0 : opentag.length
      closetag_len = closetag.nil? ? 0 : closetag.length
      Senna::sen_snip_add_cond(@handle, cond, cond.length,
                               opentag, opentag_len,
                               closetag, closetag_len)
    end

    def exec(str)
      rc, n, maxsize = Senna::sen_snip_exec(@handle, str, str.length)
      res = []
      for i in 0..n-1
        rc, r = Senna::sen_snip_get_result(@handle, i, maxsize)
        res << r if rc == 0
      end
      return res
    end

    def close
      rc = Senna::sen_snip_close(@handle)
      @handle = nil
      ObjectSpace.undefine_finalizer(self)
      return rc
    end
  end

  class Ctx
    def Ctx::connect(host='localhost', port=DEFAULT_PORT)
      handle = Senna::sen_ctx_connect(host, port, 0)
      return new(handle, nil)
    end

    def Ctx::open(path)
      db = Senna::sen_db_open(path)
      raise "sen_db_open(#{path}) failed" unless db
      handle = Senna::sen_ctx_open(db, 1)
      return new(handle, db)
    end

    def Ctx::create(path, flags=0, encoding=ENC_DEFAULT)
      db = Senna::sen_db_create(path, flags, encoding)
      raise "sen_db_create(#{path}) failed" unless db
      handle = Senna::sen_ctx_open(db, 1)
      return new(handle, db)
    end

    def Ctx.callback(handle, db)
      proc {
        Senna::sen_ctx_close(handle)
        Senna::sen_db_close(db) if db
      }
    end

    def initialize(handle, db)
      @handle = handle
      @db = db
      ObjectSpace.define_finalizer(self, Ctx.callback(@handle, @db))
    end

    def send(message)
      return Senna::sen_ctx_send(@handle, message, message.length, 0)
    end

    def recv()
      return Senna::sen_ctx_recv(@handle)
    end

    def exec(*messages)
      res = nil
      messages.each {|message|
        rc = send(message)
        raise IOError, 'send error' if rc != RC_SUCCESS
        res = []
        loop {
          rc, value, flags = recv
          raise IOError, 'recv error' if rc != RC_SUCCESS
          res << value
          break if (flags & CTX_MORE) == 0
        }
      }
      res
    end

  end

  def Senna::get_select_optarg(mode, similarity_threshold_max_interval=0, weight_vector=nil, &func)
    i = Senna::SelectOptarg.new
    i.mode = mode
    case mode
    when SEL_NEAR
      i.max_interval = similarity_threshold_max_interval
    when SEL_SIMILAR
      i.similarity_threshold = similarity_threshold_max_interval
    end
    i.weight_vector = weight_vector
    i.func = proc {|records, docid, secno, func_arg| r = Records.new(records, false); return func.call(r, docid, secno) || 0} if func
    return i
  end

  def Senna::get_group_optarg(mode, key_size, &func)
    i = Senna::GroupOptarg.new
    i.mode = mode
    i.func = proc {|records, rh, func_arg| r = Record.new(records, rh, key_size); return func.call(r) || 0} if func
    i.key_size = key_size
    return i
  end

  def Senna::get_sort_optarg(mode, &compar)
    i = Senna::SortOptarg.new
    i.mode = mode
    i.compar = proc {|records1, rh1, records2, rh2, compar_arg|
      r1 = Record.new(records1, rh1);
      r2 = Record.new(records2, rh2);
      return compar.call(r1, r2) || 0} if compar
    return i
  end

  def Senna::get_set_sort_optarg(mode, &compar)
    i = Senna::SetSortOptarg.new
    i.mode = mode
    i.compar = proc {|set1, eh1, set2, eh2, compar_arg|
      s1 = Set.new(set1, false);
      s2 = Set.new(set2, false);
      return compar.call(s1, eh1, s2, eh2) || 0} if compar
    return i
  end

  def Senna::logger_info_set(level, flags, &func)
    @@logger_info = LoggerInfo.new
    @@logger_info.max_level = level
    @@logger_info.flags = flags
    @@logger_info.func = func
    sen_logger_info_set(@@logger_info)
  end

  def Senna::log(level, fmt, *args)
    msg = format(fmt, *args)
    sen_logger_put(level, '', 0, '', '%s', msg)
  end

  def Senna::str_normalize(str, encoding, flags)
    l, = sen_str_normalize(str, str.size, encoding, flags, 0)
    return sen_str_normalize(str, str.size, encoding, flags, l + 1)[1]
  end
end
