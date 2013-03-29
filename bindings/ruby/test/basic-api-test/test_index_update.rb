# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.

require 'test/unit'
require 'senna'
require 'nkf'
require 'test_tools'

$KCODE = 'e'

class IndexUpdateTest < Test::Unit::TestCase
  SOME = 1000
  MANY = 20000
  MAX_KEY_LEN = 8191
  MAX_KEY_NUM = 0x1000_0000 - 1
  def test_upd_sel
    index = TestIndex::create
    index.upd('key', nil, 'test')
    rcs = index.sel('test')
    assert_equal(1, rcs.nhits)
  end

  def test_upd_upd_sel
    index = TestIndex::create
    index.upd('key', nil, 'original')
    index.upd('key', 'original', 'changed')
    rcs = index.sel('original')
    assert_nil(rcs)
    rcs = index.sel('changed')
    assert_equal(1, rcs.nhits)
  end

  def test_many_upd_sel
    index = TestIndex::create
    MANY.times {|i|
      index.upd(i.to_s, nil, 'test')
    }
    rcs = index.sel('test')
    assert_equal(MANY, rcs.nhits)
  end

  def test_some_upd_some_sel
    index = TestIndex::create
    SOME.times {|i|
      rcs = index.sel('test')
      assert_equal(i, rcs.nhits) if rcs
      index.upd(i.to_s, nil, 'test')
      GC.start if i % 100 == 0
    }
  end

  def test_non_normalize_eng_upd
    index = TestIndex::create(0, 0, 0, Senna::ENC_NONE)
    index.upd('key', nil, 'TeSt')
    rcs = index.sel('tEsT')
    assert_nil(rcs)
  end

  def test_normalize_eng_upd
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE, 0, Senna::ENC_NONE)
    index.upd('key', nil, 'TeSt')
    rcs = index.sel('tEsT')
    assert_equal(1, rcs.nhits)
    index.upd('key', 'teST', 'Changed')
    rcs = index.sel('tEsT')
    assert_nil(rcs)
    rcs = index.sel('CHANGED')
    assert_equal(1, rcs.nhits)
  end

  def test_normalize_eucjp_upd
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE, 0, Senna::ENC_EUC_JP)
    index.upd('¥­¡¼', nil, '£Ô£å£Ó£ô')
    rcs = index.sel('£ô£Å£ó£Ô')
    assert_equal(1, rcs.nhits)
    index.upd('¥­¡¼', '£ô£å£Ó£Ô', 'ŽÁŽªŽÝŽ¼ŽÞ')
    rcs = index.sel('£ô£Å£ó£Ô')
    assert_nil(rcs)
    rcs = index.sel('¥Á¥§¥ó¥¸')
    assert_equal(1, rcs.nhits)
  end

  def test_normalize_sjis_upd
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE | Senna::INDEX_NGRAM, 0, Senna::ENC_SJIS)
    index.upd('¥­¡¼', nil, NKF.nkf('-Es', '£Ô£å£Ó£ô'))
    rcs = index.sel(NKF.nkf('-Es', '£ô£Å£ó£Ô'))
    assert_equal(1, rcs.nhits)
    index.upd('¥­¡¼', NKF.nkf('-Es', '£ô£å£Ó£Ô'), NKF.nkf('-Esx', 'ŽÁŽªŽÝŽ¼ŽÞ'))
    rcs = index.sel(NKF.nkf('-Es', '£ô£Å£ó£Ô'))
    assert_nil(rcs)
    rcs = index.sel(NKF.nkf('-Es', '¥Á¥§¥ó¥¸'))
    assert_equal(1, rcs.nhits)
  end

  def test_normalize_utf8_upd
    index = TestIndex::create(0, Senna::INDEX_NORMALIZE | Senna::INDEX_NGRAM, 0, Senna::ENC_UTF8)
    index.upd('¥­¡¼', nil, NKF.nkf('-Ew', '£Ô£å£Ó£ô'))
    rcs = index.sel(NKF.nkf('-Ew', '£ô£Å£ó£Ô'))
    assert_equal(1, rcs.nhits)
    index.upd('¥­¡¼', NKF.nkf('-Ew', '£ô£å£Ó£Ô'), NKF.nkf('-Ewx', 'ŽÁŽªŽÝŽ¼ŽÞ'))
    index.upd('¥­¡¼2', nil, NKF.nkf('-Ew', '­Î'))
    index.upd('¥­¡¼3', nil, NKF.nkf('-Ew', '¥ª¥ì¥ó¥·¡«'))
    rcs = index.sel(NKF.nkf('-Ew', '£ô£Å£ó£Ô'))
    assert_nil(rcs)
    ['¥Á¥§¥ó¥¸', '¥ß¥ê¥Ð¡¼¥ë', '¥ª¥ì¥ó¥¸'].each {|word|
      rcs = index.sel(NKF.nkf('-Ew', word))
      assert_equal(1, rcs.nhits)
    }
  end

  def test_fixnum_key_upd
# comment out for 64bit machines
#    index = TestIndex::create(MAX_KEY_NUM.size)
    index = TestIndex::create(4)
    index.upd(0, nil, 'test 0')
    index.upd(1, nil, 'test 1')
    index.upd(MAX_KEY_NUM, nil, "test #{MAX_KEY_NUM}")
    rcs = index.sel('test')
    assert_equal(3, rcs.nhits)

    assert_not_equal(0, rcs.find(0))
    assert_not_equal(0, rcs.find(1))
    assert_not_equal(0, rcs.find(MAX_KEY_NUM))
    index.upd(0, 'test 0', 'updated 0')
    index.upd(1, 'test 1', 'updated 1')
    index.upd(MAX_KEY_NUM, "test #{MAX_KEY_NUM}", "updated #{MAX_KEY_NUM}")
    rcs = index.sel('test')
    assert_nil(rcs)
    rcs = index.sel('updated')
    assert_equal(3, rcs.nhits)
    assert_not_equal(0, rcs.find(0))
    assert_not_equal(0, rcs.find(1))
    assert_not_equal(0, rcs.find(MAX_KEY_NUM))
  end
  
  def test_max_key_upd
    index = TestIndex::create
    10.times {|i|
      key = i.to_s * (MAX_KEY_LEN - 1)
      index.upd(key, nil, 'test')
      rcs = index.sel('test')
      assert_equal(1, rcs.nhits)
      assert_not_equal(0, rcs.find(key))
      index.upd(key, 'test', 'newvalue')
      rcs = index.sel('test')
      assert_nil(rcs)
      rcs = index.sel('newvalue')
      assert_equal(i + 1, rcs.nhits)
      assert_not_equal(0, rcs.find(key))
    }
  end

  def test_ngram_eng
    index = TestIndex::create(0, Senna::INDEX_NGRAM | Senna::INDEX_NORMALIZE)
    index.upd('key', nil, 'test')
    index.upd('key2', nil, 'te')
    index.upd('key3', nil, 's')
    rcs = index.sel('test')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('te')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('s')
    assert_equal(1, rcs.nhits) 
  end

  def test_ngram_ja
    index = TestIndex::create(0, Senna::INDEX_NGRAM | Senna::INDEX_NORMALIZE,
                              0, Senna::ENC_EUC_JP)
    index.upd('key', nil, '¥Æ¥¹¥È')
    index.upd('key2', nil, '¥Æ¥¹')
    index.upd('key3', nil, '¥Æ')
    rcs = index.sel('¥Æ¥¹¥È')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('¥Æ¥¹')
    assert_equal(2, rcs.nhits)
    rcs = index.sel('¥Æ')
    assert_equal(3, rcs.nhits)
  end

  def test_ngram_normalize
    index = TestIndex::create(0, Senna::INDEX_NGRAM | Senna::INDEX_NORMALIZE)
    index.upd('key', nil, 'TeSt')
    rcs = index.sel('tEsT')
    assert_equal(1, rcs.nhits)
  end

  def test_ngram_split_alpha
    # In future, this test will not require Senna::INDEX_NORMALIZE flag.
    index = TestIndex::create(0, Senna::INDEX_NGRAM | Senna::INDEX_NORMALIZE | Senna::INDEX_SPLIT_ALPHA)
    index.upd('key', nil, 'alphabet')
    index.upd('key2', nil, 'al')
    rcs = index.sel('alphabet')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('al')
    assert_equal(2, rcs.nhits)
    rcs = index.sel('ph')
    assert_equal(1, rcs.nhits)
  end

  def test_ngram_split_digit
    # In future, this test will not require Senna::INDEX_NORMALIZE flag.
    index = TestIndex::create(0, Senna::INDEX_NGRAM | Senna::INDEX_NORMALIZE | Senna::INDEX_SPLIT_DIGIT)
    index.upd('key', nil, '1234567890')
    index.upd('key2', nil, '12')
    rcs = index.sel('1234567890')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('12')
    assert_equal(2, rcs.nhits)
    rcs = index.sel('34')
    assert_equal(1, rcs.nhits)
  end

  def test_ngram_split_symbol
    # In future, this test will not require Senna::INDEX_NORMALIZE flag.
    index = TestIndex::create(0, Senna::INDEX_NGRAM | Senna::INDEX_NORMALIZE | Senna::INDEX_SPLIT_SYMBOL)
    index.upd('key', nil, '!"#$%&()=-')
    index.upd('key2', nil, '()')
    rcs = index.sel('!"#$%&()=-')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('()')
    assert_equal(2, rcs.nhits)
    rcs = index.sel('#$')
    assert_equal(1, rcs.nhits)
  end

  def test_delimited_eng
    index = TestIndex::create(0, Senna::INDEX_DELIMITED)
    index.upd('key', nil, 'I\'m from Brasil')
    index.upd('key2', nil, 'fr')
    rcs = index.sel('I\'m')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('fr')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('Brasil')
    assert_equal(1, rcs.nhits)
  end

  def test_delimited_ja
    index = TestIndex::create(0, Senna::INDEX_DELIMITED, 0, Senna::ENC_EUC_JP)
    index.upd('key', nil, 'ÆüËÜ¸ì ¤Ç¤â ¤³¤¦¤·¤Æ ¥¹¥Ú¡¼¥¹ ¤Ç ¶èÀÚ¤Ã¤Æ ¥¤¥ó¥Ç¥Ã¥¯¥¹ ¤Ç¤­¤ë ¤è')
    index.upd('key2', nil, '¤¦¤·')
    rcs = index.sel('ÆüËÜ¸ì')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('¤¦¤·')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('¤Ç')
    assert_equal(1, rcs.nhits)
  end

  def test_delimited_normalize
    index = TestIndex::create(0, Senna::INDEX_DELIMITED | Senna::INDEX_NORMALIZE)
    index.upd('key', nil, 'TeSt de GOZARU')
    rcs = index.sel('tEsT')
    assert_equal(1, rcs.nhits)
    rcs = index.sel('gozaru')
    assert_equal(1, rcs.nhits)
  end
end

