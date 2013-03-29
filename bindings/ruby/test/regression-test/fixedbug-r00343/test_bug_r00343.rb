# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'

class FixedBugRev00343Test < Test::Unit::TestCase
  RECORDS = 32788
  def test_delete_with_score_bug
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)

    # insert
    for i in 1..RECORDS
      index.update(i.to_s, 1, nil, 'test')
    end

    # delete
    for i in 1..RECORDS
      index.update(i.to_s, 1, 'test', nil)
    end

    rcs = index.query_exec(Senna::Query::open('test'), nil, Senna::SEL_OR)
    assert_equal(0, rcs.nhits)
  end
end
