# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'

class FixedBugRev00643Test < Test::Unit::TestCase
  def test_split_alpha_huge_record_bug
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE + Senna::INDEX_NGRAM + Senna::INDEX_SPLIT_ALPHA)
    str = 'aaaaiiiiuuuueeee'
    17.times { str = str + str }
    #p str.size
    index.update(1, 1, nil, str)

    rcs = index.query_exec(Senna::Query::open('uuuu'), nil, Senna::SEL_OR)
    assert_equal(1, rcs.nhits)
  end
end
