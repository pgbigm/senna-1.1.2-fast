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

class RecordsAdvancedTest < Test::Unit::TestCase
  def test_open
    # ToDo use select and check whether ok or not
    recs = Senna::Records::open(Senna::REC_DOCUMENT, Senna::REC_NONE, 0)
    recs = Senna::Records::open(Senna::REC_DOCUMENT, Senna::REC_DOCUMENT, 0)
    recs = Senna::Records::open(Senna::REC_NONE, Senna::REC_NONE, 0)
    recs = Senna::Records::open(Senna::REC_SECTION, Senna::REC_POSITION, 0)
    recs = Senna::Records::open(Senna::REC_POSITION, Senna::REC_USERDEF, 0)
  end

  def test_set_operation
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('a', 1, nil, 'a')
    index.update('b', 1, nil, 'b')
    index.update('a&b', 1, nil, 'a b')
    index.update('c', 1, nil, 'c')

    # a or b
    rcsa = index.select('a')
    rcsb = index.select('b')
    rcsa.union(rcsb)

    assert_equal(3, rcsa.nhits)

    # b \ a
    rcsb = index.select('b')
    rcsa = index.select('a')
    rcsb.subtract(rcsa)

    assert_equal(1, rcsb.nhits)
    assert_not_equal(0, rcsb.find('b'))

    # a and b
    rcsa = index.select('a')
    rcsb = index.select('b')
    rcsa.intersect(rcsb)

    assert_equal(1, rcsa.nhits)
    assert_not_equal(0, rcsa.find('a&b'))

    # a difference b
    rcsa = index.select('a')
    rcsb = index.select('b')
    rcsa.difference(rcsb)

    assert_equal(1, rcsa.nhits)
    assert_equal(0, rcsa.find('a&b'))
    assert_equal(1, rcsb.nhits)
    assert_equal(0, rcsb.find('a&b'))
  end

  def test_sort
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test')
    index.update('2', 1, nil, Senna::Values::open('test', 1000))

    rcs = index.select('test')
    rcs.sort(2)
    rcs.next
    assert_equal('2', rcs.curr_key)
    rcs.next
    assert_equal('1', rcs.curr_key)

    rcs.sort(2, Senna::get_sort_optarg(Senna::SORT_ASCENDING))
    rcs.next
    assert_equal('1', rcs.curr_key)
    rcs.next
    assert_equal('2', rcs.curr_key)
    rh = rcs.curr_rec

    rcs.sort(2, Senna::get_sort_optarg(Senna::SORT_DESCENDING) {|r1, r2|
      ret = 0
      ret = 1 if rh.handle == r1.handle
      ret = -1 if rh.handle == r2.handle
      return ret
    })
    rcs.next
    assert_equal('2', rcs.curr_key)
    rcs.next
    assert_equal('1', rcs.curr_key)

    # ToDo: limit check
  end

  def test_group
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test')
    index.update('1', 2, nil, 'test document')
    index.update('2', 1, nil, 'test document')
    index.update('3', 1, nil, 'test other')

    rcs = Senna::Records::open(Senna::REC_SECTION, Senna::REC_NONE, 0)
    index.select('test', rcs)
    assert_equal(4, rcs.nhits)
    rcs.group(0)
    assert_equal(3, rcs.nhits)

    rcs = Senna::Records::open(Senna::REC_SECTION, Senna::REC_NONE, 0)
    index.select('test', rcs)
    rcs.group(0, Senna::get_group_optarg(Senna::SORT_DESCENDING, 2) {|r|
      # group key is string
      r.section.to_s
    })
    assert_equal(2, rcs.nhits)

    # ToDo: limit check
  end
  # curr_rec, at includes record_info test
  def test_curr_rec
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test')

    rcs = index.select('test')
    rcs.next
    r = rcs.curr_rec(2)
    assert_equal('1', r.key)
    assert_equal(2, r.keysize)
    assert_equal(0, r.section)
    assert_equal(0, r.pos)
    assert_equal(rcs.curr_score, r.score)
    assert_equal(1, r.n_subrecs)
  end

  def test_at
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test')
    index.update('1', 2, nil, 'test')

    rcs = index.select('test')
    r = rcs.at('1', 1, 0, 2)
    rcs.rewind
    rcs.next
    assert_equal('1', r.key)
    assert_equal(2, r.keysize)
    assert_equal(0, r.section)
    assert_equal(0, r.pos)
    assert_equal(rcs.curr_score, r.score)
    assert_equal(2, r.n_subrecs)
  end
  # ToDo: test record_subrec_info
end

