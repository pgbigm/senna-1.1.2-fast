# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'

$KCODE = 'e'

class FixedBugRev00308Test < Test::Unit::TestCase
  NRRECORS = 152900
  def test_bighitnum_bug
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)

    # insert NRRECORS records
    for i in 1..NRRECORS
      index.update(i.to_s, 1, nil, 'test')
    end

    # 'test' returns a huge number of results, and 'hoge' returns no results.
    rcs = index.query_exec(Senna::Query::open('*D+ test hoge'), nil, Senna::SEL_OR)
    assert_equal(0, rcs.nhits)
  end
end
