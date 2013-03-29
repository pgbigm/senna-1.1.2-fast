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

class FixedBugRev00277Test < Test::Unit::TestCase
  def test_multisection_bug
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)

    # insert section 1, 2
    index.update('1', 1, nil, 'test')
    index.update('1', 2, nil, 'word')

    # delete section 1, 2
    index.update('1', 1, 'test', nil)
    index.update('1', 2, 'word', nil)

    # select 'test'
    rcs = index.sel('word')
    assert_nil(rcs)
  end
end
