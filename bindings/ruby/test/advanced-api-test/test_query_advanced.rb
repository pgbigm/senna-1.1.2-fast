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

class QueryAdvancedTest < Test::Unit::TestCase
  def test_open
    query = Senna::Query::open('test query is here')
    assert_equal(Senna::RC_SUCCESS, query.close)
  end

  def test_rest
    query = Senna::Query::open('test', Senna::SEL_OR, 0)
    assert_equal('test', query.rest)

    query = Senna::Query::open('1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16', Senna::SEL_OR, 10)
    assert_equal('11 12 13 14 15 16', query.rest)

    query = Senna::Query::open('*E-7 test query', Senna::SEL_OR, 1)
    assert_equal('query', query.rest)

    query = Senna::Query::open('*E-7*D+ *S10"test" *N10"document" is here', Senna::SEL_OR, 1)
    assert_equal('*N10"document" is here', query.rest)

    query = Senna::Query::open('*E-7*D+ "test document" is here', Senna::SEL_OR, 2)
    assert_equal('here', query.rest)

    query = Senna::Query::open('*E-7*D+ test OR document is here', Senna::SEL_OR, 2)
    assert_equal('is here', query.rest)

    query = Senna::Query::open('*E-7*D+ (test (document OR paper) is) OR here', Senna::SEL_OR, 4)
    assert_equal(') OR here', query.rest)

    query = Senna::Query::open('"\\ \\試 \\"" rest', Senna::SEL_OR, 1)
    assert_equal('rest', query.rest)
  end

  def test_exec
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('a', 1, nil, 'a')
    index.update('b', 1, nil, 'b')
    index.update('a&b', 1, nil, 'a b')
    index.update('other', 1, nil, 'other')
    index.update('phrase search', 1, nil, 'phrase search')
    index.update('"quoted string"', 1, nil, '"quoted string"')

    # equivalent test
    rc1 = index.query('a', Senna::SEL_OR, 32, Senna::ENC_DEFAULT)
    query = Senna::Query::open('a', Senna::SEL_OR, 32, Senna::ENC_DEFAULT)
    rc2 = index.query_exec(query, nil, Senna::SEL_OR)
    rc3 = query.exec(index, nil, Senna::SEL_OR)

    assert_equal(2, rc1.nhits)
    assert_not_equal(0, rc1.find('a'))
    assert_equal(2, rc2.nhits)
    assert_not_equal(0, rc2.find('a'))
    assert_equal(2, rc3.nhits)
    assert_not_equal(0, rc3.find('a'))

    # default op test
    rc = index.query('a b', Senna::SEL_OR)
    assert_equal(3, rc.nhits)
    scorea = rc.find('a')
    scoreab = rc.find('a&b')

    rc = index.query('a b', Senna::SEL_AND)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a&b'))

    rc = index.query('a b', Senna::SEL_BUT)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a'))

    rc = index.query('a b', Senna::SEL_ADJUST)
    assert_equal(2, rc.nhits)
    assert_equal(scorea, rc.find('a'))
    # ToDo: now, Senna is not implemented to be able to search with SEL_ADJUST
    # assert(scoreab < rc.find('a&b'))

    # exec op test
    querya = Senna::Query::open('a')
    queryb = Senna::Query::open('b')
    rc = queryb.exec(index)
    querya.exec(index, rc, Senna::SEL_OR)
    assert_equal(3, rc.nhits)
    scoreb = rc.find('b')
    scoreab = rc.find('a&b')

    rc = queryb.exec(index)
    querya.exec(index, rc, Senna::SEL_AND)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a&b'))

    rc = queryb.exec(index)
    querya.exec(index, rc, Senna::SEL_BUT)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('b'))

    rc = queryb.exec(index)
    querya.exec(index, rc, Senna::SEL_ADJUST)
    assert_equal(2, rc.nhits)
    assert_equal(scoreb, rc.find('b'))
    # ToDo: now, Senna is not implemented to be able to search with SEL_ADJUST
    # assert(scoreab < rc.find('a&b'))

    # query pragma test
    rc = index.query('*DOR a b', Senna::SEL_AND)
    assert_equal(3, rc.nhits)

    rc = index.query('*D+ a b', Senna::SEL_OR)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a&b'))

    rc = index.query('*D- a b', Senna::SEL_OR)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a'))

    # query opetator test
    rc = index.query('+a +b', Senna::SEL_OR)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a&b'))

    rc = index.query('+a -b', Senna::SEL_OR)
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('a'))

    rc = index.query('a OR b', Senna::SEL_AND)
    assert_equal(3, rc.nhits)
    scorea = rc.find('a')
    scoreab = rc.find('a&b')

    rc = index.query('a ~b', Senna::SEL_AND)
    assert_equal(2, rc.nhits)
    assert_equal(scorea, rc.find('a'))
    assert(scoreab > rc.find('a&b'))

    rc = index.query('<a b', Senna::SEL_OR)
    assert(rc.find('a') < rc.find('b'))

    rc = index.query('a >b', Senna::SEL_OR)
    assert(rc.find('a') > rc.find('b'))

    rc = index.query('(a OR b) -(+a +b)')
    assert_equal(2, rc.nhits)
    assert_not_equal(0, rc.find('a'))
    assert_not_equal(0, rc.find('b'))

    rc = index.query(('(' * 31) << 'a', Senna::SEL_OR, 32)
    assert_equal(2, rc.nhits)

    rc = index.query(('()' * 15) << 'b', Senna::SEL_OR, 16)
    assert_equal(2, rc.nhits)

    rc = index.query('ot*')
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('other'))

    rc = index.query('"phrase search"')
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('phrase search'))

    rc = index.query('"\\"quoted string\\""')
    assert_equal(1, rc.nhits)
    assert_not_equal(0, rc.find('"quoted string"'))
  end

  def test_exec_escalation
    # ToDo: modify test more suitable for Senna::SEL_UNSPLIT
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE, 0, Senna::ENC_EUC_JP)
    index.update('1', 1, nil, Senna::Values::open('それなら君が代表監督だ', 5))
    index.update('2', 1, nil, Senna::Values::open('君が代', 5))

    rc = index.query('*E-1 君が代')
    assert_equal(1, rc.nhits)
    score_exact = rc.find('2')

    rc = index.query('*E-1 君が代表監督')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-2 君が代表監督')
    assert_equal(0, rc.nhits)

    rc = index.query('*E-3 君が代表監督')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-3 君が代')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-4 君が代')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-5 君が代')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-6 君が代')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-7 君が代')
    assert_equal(1, rc.nhits)

    # ToDo: test escalation threshold
  end

  def test_multisection_weight
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE | Senna::INDEX_DELIMITED)
    (1..256).each{|c|
      i = c * 4
      index.update('1', i, nil, i.to_s)

      # if W with no parameters is specified, all sections are ignored.
      rc = index.query("*W #{i.to_s}")
      assert_equal(0, rc.nhits)

      rc = index.query("*W#{i.to_s}:0 #{i.to_s}")
      assert_equal(0, rc.nhits)

      rc = index.query("*W#{(i + 1).to_s}:0,#{i.to_s}:1 #{i.to_s}")
      assert_equal(1, rc.nhits)

      rc = index.query("*W#{i.to_s}:1 #{i.to_s}")
      assert_equal(1, rc.nhits)

      # if weight is not specified, weight is 1
      rc = index.query("*W#{i.to_s},#{(i + 1).to_s}:0, #{i.to_s}")
      assert_equal(1, rc.nhits)

      score1 = rc.find(i.to_s)
      rc = index.query("*W#{i.to_s}:2 #{i.to_s}")
      assert_equal(1, rc.nhits)
      score2 = rc.find(i.to_s)
      assert_equal(score2, score1 * 2)

      rc = index.query("*W#{i.to_s}:-1 #{i.to_s}")
      assert_equal(1, rc.nhits)
      scorem1 = rc.find(i.to_s)
      assert_equal(scorem1, -score1)
    }
  end

  def test_exec_multi_pragma
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE | Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test document')
    index.update('1', 2, nil, 'senna')

    rc = index.query('*E-7,3*DOR*W2:1 sen')
    assert_equal(1, rc.nhits)

    rc = index.query('*E-7*D+*W1 te')
    assert_equal(1, rc.nhits)
  end
end

