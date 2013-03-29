# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'

class IndexCreateTest < Test::Unit::TestCase
  def test_normal_create
    index = Senna::Index::create('testindex')
    assert_equal('testindex', index.path)
    index.remove
  end

  def test_create_rename
    index = Senna::Index::create('oldindex')
    index.rename('newindex')
    assert_equal('newindex', index.path)
    index.remove
  end

  def test_create_close_open
    index = Senna::Index::create('create_close_open')
    index.close
    index = Senna::Index::open('create_close_open')
    assert_equal('create_close_open', index.path)
    index.remove
  end
end

