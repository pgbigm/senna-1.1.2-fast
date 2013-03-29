# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'senna'
require 'tmpdir'

module TestTools
end

class TestIndex < Senna::Index
  def TestIndex.callback(handle, path)
    proc { Senna::sen_index_close(handle); Senna::sen_index_remove(path) }
  end

  def TestIndex::create(key_size=0, flags=0, initial_n_segments=0, encoding=Senna::ENC_DEFAULT)
    path = Dir.tmpdir + '/sennaindex-' + caller.first[/:in \`(.*?)\'\z/, 1]
    handle = Senna::sen_index_create(path, key_size, flags, initial_n_segments, encoding)
    return new(handle, path)
  end

  def initialize(handle, path)
    @handle = handle
    ObjectSpace.define_finalizer(self, TestIndex.callback(handle, path))
  end
end

