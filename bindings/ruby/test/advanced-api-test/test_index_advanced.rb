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

class IndexAdvancedTest < Test::Unit::TestCase
  def test_create_with_keys
    sym = Senna::Sym::create('ext_key')
    symid = sym.get('key1')
    index = sym.index_create_with_keys('create_with_keys')
    assert_equal('create_with_keys', index.path)
    assert_equal(symid, index.keys.get('key1'))
    index.remove
    sym.remove
  end

  def test_open_with_keys
    index = Senna::Index.create('open_with_keys')
    index.upd('key2', nil, 'test')
    index.close
    sym = Senna::Sym::create('ext_key')
    sym.get('key1')
    symid = sym.get('key2')
    index = sym.index_open_with_keys('open_with_keys')
    assert_equal('open_with_keys', index.path)
    assert_equal(symid, index.keys.get('key2'))               # check whether keys is replaced
    index.remove
    sym.remove
  end

  def test_update
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('key1', 1, nil, 'test')
    assert_equal(1, index.select('test').nhits)

    index.update('key1', 1, 'test', 'changed')
    assert_equal(1, index.select('changed').nhits)
  end

  def test_update_multisection
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('key1', 1, nil, 'test')
    index.update('key1', 2, nil, 'document')
    index.update('key2', 1, nil, 'document')
    index.update('key3', 3, nil, 'other')
    
    assert_equal(1, index.select('test').nhits)
    assert_equal(2, index.select('document').nhits)
    assert_equal(1, index.select('other').nhits)              # check for isolated section
    assert_equal(0, index.select('nohit').nhits)

    index.update('key3', 3, 'other', 'changed')
    assert_equal(1, index.select('changed').nhits)
  end

  def test_update_multivalue
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    vals = Senna::Values::open('test', 1)
    vals.add('document', 2)
    index.update('key1', 1, nil, vals)
    vals = Senna::Values::open('document', 1)
    vals.add('test', 2)
    index.update('key2', 1, nil, vals)
    
    rcs = index.select('test')
    assert(rcs.find('key1') < rcs.find('key2'))
    rcs = index.select('document')
    assert(rcs.find('key1') > rcs.find('key2'))
    
    vals2 = Senna::Values::open('changed', 1)
    vals2.add('document', 2)
    index.update('key2', 1, vals, vals2)
    assert_equal(1, index.select('changed').nhits)
    rcs = index.select('document')
    assert_equal(rcs.find('key1'), rcs.find('key2'))
  end

  def test_update_multisection_multivalue
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    vals = Senna::Values::open('test', 1)
    vals.add('document', 2)
    index.update('key1', 1, nil, vals)
    vals = Senna::Values::open('is', 3)
    vals.add('here', 4)
    index.update('key1', 2, nil, vals)

    vals = Senna::Values::open('here', 2)
    vals.add('is', 4)
    index.update('key2', 1, nil, vals)
    vals = Senna::Values::open('document', 1)
    vals.add('of test', 3)
    index.update('key2', 2, nil, vals)

    rcs = index.select('test')
    assert_equal(2, rcs.nhits)
    assert(rcs.find('key1') < rcs.find('key2'))

    rcs = index.select('here')
    assert_equal(2, rcs.nhits)
    assert(rcs.find('key1') > rcs.find('key2'))

    rcs = index.select('of')
    assert_equal(1, rcs.nhits)

    assert_equal(0, index.select('nohit').nhits)

    vals2 = Senna::Values::open('changed', 1)
    vals2.add('document', 2)

    index.update('key2', 2, vals, vals2)

    assert_equal(1, index.select('changed').nhits)

    rcs = index.select('document')
    assert_equal(rcs.find('key1'), rcs.find('key2'))
  end

  def test_select
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('a', 1, nil, 'a')
    index.update('b', 1, nil, 'b')
    index.update('a&b', 1, nil, 'a b')
    index.update('c', 1, nil, 'c')

    assert_equal(2, index.select('a').nhits)
    assert_equal(2, index.select('b').nhits)
    assert_equal(1, index.select('a b').nhits)

    # a or b
    rcs = index.select('a')
    index.select('b', rcs)

    assert_equal(3, rcs.nhits)
    assert_equal(0, rcs.find('c'))

    # b and a
    rcs = index.select('b')
    index.select('a', rcs, Senna::SEL_AND)

    assert_equal(1, rcs.nhits)
    assert_not_equal(0, rcs.find('a&b'))

    # (b or a) \ b
    rcs = index.select('b')
    index.select('a', rcs)
    index.select('b', rcs, Senna::SEL_BUT)

    assert_equal(1, rcs.nhits)
    assert_not_equal(0, rcs.find('a'))

    # adjust
    rcs = index.select('a')
    scoreuni = rcs.find('a')
    scoreand = rcs.find('a&b')
    index.select('b', rcs, Senna::SEL_ADJUST)
    
    assert_equal(2, rcs.nhits)
    assert_equal(scoreuni, rcs.find('a'))
    assert(scoreand < rcs.find('a&b'))
  end

  def test_select_optarg_mode
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test document is here')

    rcs = index.select('test', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT))
    assert_equal(1, rcs.nhits)
    rcs = index.select('tes', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT))
    assert_equal(0, rcs.nhits)
    # Now, Senna doesn't make an index which contains infomation for suffix search in english words. 
    rcs = index.select('tes', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_PARTIAL))
    assert_equal(1, rcs.nhits)
    rcs = index.select('est doc', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_PARTIAL))
    assert_equal(0, rcs.nhits)
    rcs = index.select('test', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_UNSPLIT))
    assert_equal(1, rcs.nhits)

    rcs = index.select('test here', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_NEAR, 2))
    assert_equal(1, rcs.nhits)
    rcs = index.select('test here', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_NEAR, 1))
    assert_equal(0, rcs.nhits)

    # Note: This behavior will be changed in future
    rcs = index.select('you must test here', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_SIMILAR, 2))
    assert_equal(1, rcs.nhits)
    rcs = index.select('test here now', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_SIMILAR, 1))
    assert_equal(1, rcs.nhits)
  end

  def test_select_optarg_mode_ja
    index = TestIndex::create
    # テスト 文書 は ここ に あり ます
    index.update('1', 1, nil, 'テスト文書はここにあります')
    rcs = index.select('テスト', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT))
    assert_equal(1, rcs.nhits)
    rcs = index.select('テス', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT))
    assert_equal(0, rcs.nhits)
    # Now, Senna doesn't make an index which contains infomation for suffix search in english words. 
    rcs = index.select('テス', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_PARTIAL))
    assert_equal(1, rcs.nhits)
    rcs = index.select('スト文', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_PARTIAL))
    assert_equal(1, rcs.nhits)
    rcs = index.select('テスト', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_UNSPLIT))
    assert_equal(1, rcs.nhits)

    rcs = index.select('テスト ここ', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_NEAR, 2))
    assert_equal(1, rcs.nhits)
    rcs = index.select('テスト ここ', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_NEAR, 1))
    assert_equal(0, rcs.nhits)

    # Note: This behavior will be changed in future
    rcs = index.select('これがテスト文書', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_SIMILAR, 2))
    assert_equal(1, rcs.nhits)
    rcs = index.select('テストはここで', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_SIMILAR, 1))
    assert_equal(1, rcs.nhits)
  end

  def test_select_optarg_vector_func
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.update('1', 1, nil, 'test')
    index.update('1', 2, nil, 'document')
    index.update('2', 1, nil, 'document')
    index.update('2', 2, nil, 'other')

    # vector
    assert_equal(0, index.select('test', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT, 0, [0, 1])).nhits)
    assert_equal(1, index.select('document', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT, 0, [1, 0])).nhits)
    score = index.select('test', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT, 0, [1, 0])).find('1')
    assert(score < index.select('test', nil, Senna::SEL_OR, Senna::get_select_optarg(Senna::SEL_EXACT, 0, [2, 0])).find('1'))

    # func
    rcs = index.select('document', nil, Senna::SEL_OR, 
                       Senna::get_select_optarg(Senna::SEL_EXACT, 0, nil) {|r, docid, secno| return docid.to_i * 2 + secno - 1})
    assert_equal(2, rcs.nhits)
    assert_equal(3, rcs.find('1'))
    assert_equal(4, rcs.find('2'))
  end

  # ToDo: Confirm specification, and write test.
  def test_select_near_ngram_ja
    assert(true);
  end

  def test_info
    index = TestIndex::create(200, Senna::INDEX_DELIMITED | Senna::INDEX_NGRAM, 0, Senna::ENC_UTF8)
    info = index.info
    assert(info[0..4] === [0, 200, Senna::INDEX_DELIMITED | Senna::INDEX_NGRAM, 512, Senna::ENC_UTF8])
    assert_equal(0, info[5])
    assert_equal(0, info[7])
    assert_equal(info[6], info[8])
  end

  def test_info_num_key_lex
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    
    index.update('1', 1, nil, 'test')
    info = index.info
    assert_equal(1, info[5])
    assert_equal(1, info[7])
    
    index.update('1', 2, nil, 'document')
    info = index.info
    assert_equal(1, info[5])
    assert_equal(2, info[7])

    index.update('2', 1, nil, 'test document')
    info = index.info
    assert_equal(2, info[5])
    assert_equal(2, info[7])

    index.update('2', 1, 'test document', 'changed document')
    info = index.info
    assert_equal(2, info[5])
    assert_equal(3, info[7])

    index.update('2', 1, 'changed document', nil)
    index = index.info
    assert_equal(2, info[5])
    assert_equal(3, info[7])
  end

  # This API will be implemented in future.
  def test_related_term
    assert(true)
  end
end

