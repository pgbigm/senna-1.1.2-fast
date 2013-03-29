# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.

require 'test/unit'
require 'senna'

class SymCreateTest < Test::Unit::TestCase
  def test_sym_create
    sym = Senna::Sym::create('testsym',20,Senna::SYM_WITH_SIS,Senna::ENC_EUC_JP)
    syminfo = sym.info
    assert_equal(20, syminfo['key_size'])
    assert_equal(0x80000000, syminfo['flags'])
    assert_equal(0x0002, syminfo['encoding'])
    assert_equal(0, syminfo['nrecords'])
    sym.remove
    sym = Senna::Sym::create('testsym2')
    syminfo = sym.info
    assert_equal(0, syminfo['flags'])
#   automatically replace enc_default into SENNA_DEFAULT_ENCODING
#   assert_equal(0, syminfo['encoding'])
    assert_equal(0, syminfo['key_size'])
    assert_equal(0, syminfo['nrecords'])
    sym.remove
  end

  def test_sym_update
    sym = Senna::Sym::create('testsym3',5,Senna::SYM_WITH_SIS,Senna::ENC_EUC_JP)
    id01 = sym.get('KEY01')
    pid01 = sym.at('KEY01')
    assert_equal(pid01, id01)
    sym.remove
    sym = Senna::Sym::create('testsym4',0,0,0)
    id01 = sym.get('KEY02')
    pid01 = sym.at('KEY02')
    assert_equal(pid01, id01)
    sym.remove

    sym = Senna::Sym::create('testsym5',6,Senna::SYM_WITH_SIS,Senna::ENC_EUC_JP)
    did01 = sym.get('DKEY01')
    pdid01 = sym.at('DKEY01')
    assert_equal(pdid01, did01)
    assert_equal(1,sym.size)
    _, dkey01 = sym.key(did01)
    assert_equal('DKEY01',dkey01)
    sym.del(dkey01)
    assert_equal(0,sym.size)
    sym.remove

    sym = Senna::Sym::create('testsym6',0,0,0)
    did01 = sym.get('DKEY02')
    pdid01 = sym.at('DKEY02')
    assert_equal(pdid01, did01)
    assert_equal(1,sym.size)
    _, dkey01 = sym.key(did01)
    assert_equal('DKEY02',dkey01)
    sym.del(dkey01)
    assert_equal(0,sym.size)
    sym.remove
  end

  def test_sym_select
    # TODO prefix_search
    # TODO suffix_search
#    sym = Senna::Sym::create('testsym7',8,Senna::SYM_WITH_SIS,Senna::ENC_EUC_JP)
#    sym.get('n349ighi')
#    sym.get('87g2skcd')
#    sym.get('u83bcus-')
#    sym.get('ake7m/_]')
#    sym.get('CPSEARCH')
#    sym.get('^1]a[zhl')
#    sym.get('91023kv.')
#    assert_equal(7,sym.size)
#    assert_equal('CPSEARCH',sym.common_prefix_search('CPSEARC'))
#    assert_equal('^1]a[zhl',sym.common_prefix_search('^1]a['))
#    assert_equal('ake7m/_]',sym.common_prefix_search('ake7m'))
#    assert_equal('91023kv.',sym.common_prefix_search('91023'))
#    assert_equal('n349ighi',sym.common_prefix_search('n349i'))
#    assert_not_equal('n349ighi',sym.common_prefix_search('91023k'))
#    sym.remove
  end

  def test_sym_tools
    # MEMO: pocket size is 14bit
    sym = Senna::Sym::create('testsym8',6,Senna::SYM_WITH_SIS,Senna::ENC_EUC_JP)
    did01 = sym.get('DKEY01')
    pdid01 = sym.at('DKEY01')
    sym.pocket_set(pdid01, 0x3FFF)
    did02 = sym.get('DKEY02')
    pdid02 = sym.at('DKEY02')
    sym.pocket_set(pdid02, 0x2AAA)
    did03 = sym.get('DKEY03')
    pdid03 = sym.at('DKEY03')
    sym.pocket_set(pdid03, 0x1555)
    did04 = sym.get('DKEY04')
    pdid04 = sym.at('DKEY04')
    sym.pocket_set(pdid04, 0x0000)
    assert_equal(pdid03, did03)
    assert_equal(4,sym.size)
    _, dkey01 = sym.key(did01)
    assert_equal(0x3FFF, sym.pocket_get(pdid01))
    assert_equal(0x2AAA, sym.pocket_get(pdid02))
    assert_equal(0x1555, sym.pocket_get(pdid03))
    assert_equal(0x0000, sym.pocket_get(pdid04))
    sym.del(dkey01)
    assert_equal(3, sym.size)
    sym.remove
  end
end

