# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'

class FixedBugRev00346Test < Test::Unit::TestCase
  def test_sen_set_reset_gc_bug
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)

    index.update(1, 1, nil, 'MySQL has now support for full-text search')
    index.update(2, 1, nil, 'Full-text indexes are called collections')
    index.update(3, 1, nil, 'Only MyISAM tables support collections')
    index.update(4, 1, nil, 'Function MATCH ... AGAINST() is used to do a search')
    index.update(5, 1, nil, 'Full-text search in MySQL implements vector space model')

    rcs = index.query_exec(Senna::Query::open('+(support collections) +foobar*'), nil, Senna::SEL_OR)
    assert_equal(0, rcs.nhits)
    rcs = index.query_exec(Senna::Query::open('+(+(support collections)) +foobar*'), nil, Senna::SEL_OR)
    assert_equal(0, rcs.nhits)
  end
end
