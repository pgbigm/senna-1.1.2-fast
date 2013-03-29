# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'

class RecordsTest < Test::Unit::TestCase
  def test_records_basic
    index = TestIndex::create
    100.times {|i|
      index.upd(i.to_s, nil, 'test')
    }
    rcs = index.sel('test')
    assert_equal(100, rcs.nhits)
    assert_not_equal(0, rcs.find('0'))
    assert_not_equal(0, rcs.find('99'))
    # block
    i = 0
    rcs.each {|key, score|
      assert_equal(i.to_s, key)
      assert_not_equal(0, score)
      i += 1
    }
    assert_equal(100, i)
    # non block
    rcs.rewind
    i = 0
    while rcs.next
      assert_equal(i.to_s, rcs.curr_key)
      assert_not_equal(0, rcs.curr_score)
      i += 1
    end
  end
end

