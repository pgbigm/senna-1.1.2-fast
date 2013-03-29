# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'
require 'nkf'

$KCODE = 'e'

def utf8(str)
  NKF.nkf('-Ew', str)
end

def euc(str)
  NKF.nkf('-eW', str)
end

class SnippetUtilityTest < Test::Unit::TestCase
  def test_open
    snip = Senna::Snip::open(nil, 80, 3, '', '', nil, Senna::ENC_DEFAULT)
    assert_equal(Senna::RC_SUCCESS, snip.close)

    snip = Senna::Snip::open(['test', 'document'])
    assert_equal(Senna::RC_SUCCESS, snip.close)

    snip = Senna::Snip::open({'test' => ['<test>', '</test>'],
                              'document' => ['<doc>', '</doc>']})
    assert_equal(Senna::RC_SUCCESS, snip.close)
  end

  def test_add
    snip = Senna::Snip::open
    assert_equal(Senna::RC_SUCCESS, snip.add('test', '<test>', '</test>'))
    assert_equal(Senna::RC_SUCCESS, snip.add('document'))
  end

  def test_exec
    # 0 length snippet
    res = Senna::Snip::open('a', 0, 3).exec('a' * 12)
    assert_equal(0, res.size)

    # no cond
    res = Senna::Snip::open(nil, 0, 3).exec('a' * 12)
    assert_equal(0, res.size)
    
    # max length snippet
    res = Senna::Snip::open('a', 4, 3).exec('a' * 12)
    assert_equal(3, res.size)
    assert_equal(['aaaa', 'aaaa', 'aaaa'], res)

    # beyond end
    res = Senna::Snip::open('a', 4, 3).exec('a' * 11)
    assert_equal(3, res.size)
    assert_equal(['aaaa', 'aaaa', 'aaa'], res)

    # leave
    res = Senna::Snip::open('a', 4, 3).exec('a' * 100)
    assert_equal(3, res.size)
    assert_equal(['aaaa', 'aaaa', 'aaaa'], res)

    # found less
    res = Senna::Snip::open('a', 4, 3).exec('a' * 8)
    assert_equal(2, res.size)
    assert_equal(['aaaa', 'aaaa'], res)

    # not found
    res = Senna::Snip::open('test', 4, 3).exec('a' * 12)
    assert_equal(0, res.size)

    # not suffcient length for keyword
    res = Senna::Snip::open('test', 3, 3).exec('test' * 3)
    assert_equal(0, res.size)

    # centering
    res = Senna::Snip::open('test', 6, 1).exec('abcdefghitestihgfedcba')
    assert_equal(1, res.size)
    assert_equal(['itesti'], res)

    # centering with odd length
    res = Senna::Snip::open('test', 21, 1).exec('abcdefghitestihgfedcba')
    assert_equal(1, res.size)
    assert_equal(['abcdefghitestihgfedcb'], res)

    # centering on start
    res = Senna::Snip::open('test', 7, 1).exec('testihgfedcba')
    assert_equal(1, res.size)
    assert_equal(['testihg'], res)

    # centering on end
    res = Senna::Snip::open('test', 7, 1).exec('abcdefghitest')
    assert_equal(1, res.size)
    assert_equal(['ghitest'], res)

    # snippet short than buffer length
    res = Senna::Snip::open('test', 40, 1).exec('abcdefghitestihgfedcba')
    assert_equal(1, res.size)
    assert_equal(['abcdefghitestihgfedcba'], res)

    # open/close tag check
    res = Senna::Snip::open({'test' => ['<test>', '</test>']}, 6, 1).exec('abcdefghitestihgfedcba')
    assert_equal(1, res.size)
    assert_equal(['i<test>test</test>i'], res)

    # open/close tag with space check
    res = Senna::Snip::open({'test' => ['<test>', '</test>']}, 12, 1).exec('this is test document')
    assert_equal(1, res.size)
    assert_equal(['s is<test> test</test> do'], res)

    # default open/close tag check
    res = Senna::Snip::open('a', 1, 1, '<opentag>', '<closetag>').exec('aaaaa')
    assert_equal(1, res.size)
    assert_equal(['<opentag>a<closetag>'], res)

    # html encoding
    res = Senna::Snip::open('<>&"', 4, 1, '<tag>', '</tag>', -1).exec('<>&"')
    assert_equal(1, res.size)
    assert_equal(['<tag>&lt;&gt;&amp;&quot;</tag>'], res)

    # not html encoding
    res = Senna::Snip::open('<>&"', 4, 1, '<tag>', '</tag>', nil).exec('<>&"')
    assert_equal(1, res.size)
    assert_equal(['<tag><>&"</tag>'], res)

    # normalize
    res = Senna::Snip::open('TeSt', 4, 1, '', '', nil, nil, Senna::SNIP_NORMALIZE).exec('tEsT')
    assert_equal(1, res.size)
    assert_equal(['tEsT'], res)
    
    # not normalize
    snip = Senna::Snip::open('TeSt', 4, 1, '', '', nil, nil, 0)
    assert_equal(0, snip.exec('tEsT').size)
    res = snip.exec('TeSt')
    assert_equal(1, res.size)
    assert_equal(['TeSt'], res)
  end

  def test_exec_multi
    # multi centering(adjacent)
    res = Senna::Snip::open(['test', 'doc'], 11, 1).exec('abcdtestdocdcba')
    assert_equal(1, res.size)
    assert_equal(['cdtestdocdc'], res)

    # multi centering(apart)
    res = Senna::Snip::open(['test', 'doc'], 15, 1).exec('abcdtestxxxdocdcba')
    assert_equal(1, res.size)
    assert_equal(['bcdtestxxxdocdc'], res)
    
    # multi centering(apart,all)
    res = Senna::Snip::open(['test', 'doc'], 15, 1).exec('abcdtestxxxxxxxxdocdcba')
    assert_equal(1, res.size)
    assert_equal(['testxxxxxxxxdoc'], res)
    
    # multi centering(all)
    res = Senna::Snip::open(['test', 'doc'], 11, 1).exec('testabcddoc')
    assert_equal(1, res.size)
    assert_equal(['testabcddoc'], res)

    # multi centering(start)
    res = Senna::Snip::open(['test', 'doc'], 11, 1).exec('testdocabcdefg')
    assert_equal(1, res.size)
    assert_equal(['testdocabcd'], res)
    
    # multi centering(end)
    res = Senna::Snip::open(['test', 'doc'], 11, 1).exec('abcdefgdoctest')
    assert_equal(1, res.size)
    assert_equal(['defgdoctest'], res)

    # one tag not found
    res = Senna::Snip::open(['test', 'doc'], 11, 1).exec('this is test !!!')
    assert_equal(1, res.size)
    assert_equal([' is test !!'], res)

    # overlap centering
    res = Senna::Snip::open(['test', 'doc'], 6, 2).exec('fastestdocumentdocument')
    assert_equal(2, res.size)
    assert_equal(['stestd', 'ntdocu'], res)

    # multi same cond tag
    snip = Senna::Snip::open(nil, 5, 1)
    snip.add('a', '<a>', '</a>')
    snip.add('a', '<b>', '</b>')
    res = snip.exec('a')
    assert_equal(1, res.size)
    assert_equal(['<a>a</a>'], res)

    # multitag
    res = Senna::Snip::open({ 'test' => ['<test>', '</test>'],
                              'doc' => ['<doc>', '</doc>'] },
                            10, 1).exec('this is test document')
    assert_equal(1, res.size)
    assert_equal(['s<test> test</test><doc> doc</doc>'], res)
    
    # nested tag
    res = Senna::Snip::open({ 'test' => ['<test>', '</test>'],
                              't' => ['<t>', '</t>'],
                              'te' => ['<te>', '</te>'] },
                           4, 1).exec('test')
    assert_equal(1, res.size)
    assert_equal(['<test>test</test>'], res)
  end
  
  def test_exec_ja
    # encodings
    
    # check normalize
    res = Senna::Snip::open(utf8('¤Ç'), 6, 1, '.', '.', nil, Senna::ENC_UTF8).exec(utf8('¤Æ¡«'))
    assert_equal('.¤Æ¡«.', euc(res[0]))

    res = Senna::Snip::open(utf8('¤Æ¡«'), 3, 1, '.', '.', nil, Senna::ENC_UTF8).exec(utf8('¤Ç'))
    assert_equal('.¤Ç.', euc(res[0]))
    
    # checks nested tag
    res = Senna::Snip::open( { 'ÆüËÜ' => ['<ÆüËÜ>', '</ÆüËÜ>'],
                               '¶ä¹Ô' => ['<¶ä¹Ô>', '</¶ä¹Ô>'],
                               'ÆüËÜ¶ä¹Ô' => ['<ÆüËÜ¶ä¹Ô>', '</ÆüËÜ¶ä¹Ô>'] }, 
                             8, 1, '', '', nil, Senna::ENC_EUC_JP).exec('ÆüËÜ¶ä¹Ô')
    assert_equal(['<ÆüËÜ¶ä¹Ô>ÆüËÜ¶ä¹Ô</ÆüËÜ¶ä¹Ô>'], res)

    # checks overlap
    res = Senna::Snip::open( { '¥ß¥ê' => ['<¥ß¥ê>', '</¥ß¥ê>'],
                               '¥ê¥Ð¡¼' => ['<¥ê¥Ð¡¼>', '</¥ê¥Ð¡¼>'],
                               '¥Ð¡¼¥ë' => ['<¥Ð¡¼¥ë>', '</¥Ð¡¼¥ë>'] },
                             10, 1, '', '', nil, Senna::ENC_EUC_JP).exec('¥ß¥ê¥Ð¡¼¥ë')
    assert_equal(['<¥ß¥ê>¥ß¥ê</¥ß¥ê><¥Ð¡¼¥ë>¥Ð¡¼¥ë</¥Ð¡¼¥ë>'], res)

    # checks nested tag with overlap and normalize
    snip = Senna::Snip::open( nil, 15, 1, '', '', nil, Senna::ENC_UTF8)
    snip.add(utf8('¥ß¥ê'), utf8('<¥ß¥ê>'), utf8('</¥ß¥ê>'))
    snip.add(utf8('¥ê¥Ð¡¼'), utf8('<¥ê¥Ð¡¼>'), utf8('</¥ê¥Ð¡¼>'))
    snip.add(utf8('¥Ð¡¼¥ë'), utf8('<¥Ð¡¼¥ë>'), utf8('</¥Ð¡¼¥ë>'))
    res = snip.exec(utf8('­Î'))
    assert_equal(1, res.size)
    assert_equal('<¥ß¥ê>­Î</¥ß¥ê>', euc(res[0]))

    res = Senna::Snip::open( { '.' => ['<.>', '</.>'] },
                             3, 1, '', '', nil, Senna::ENC_UTF8).exec(utf8('¡Ä'))
    assert_equal(1, res.size)
    assert_equal('<.>¡Ä</.>', euc(res[0]))

    snip = Senna::Snip::open(nil, 18, 1, '', '', nil, Senna::ENC_UTF8)
    snip.add(utf8('¥ê¥á¡¼'), utf8('<¥ê¥á¡¼>'), utf8('</¥ê¥á¡¼>'))
    snip.add(utf8('¥á¡¼¥È'), utf8('<¥á¡¼¥È>'), utf8('</¥á¡¼¥È>'))
    snip.add(utf8('¥ë¥»¥ó¥Á¥È'), utf8('<¥ë¥»¥ó¥Á¥È>'), utf8('</¥ë¥»¥ó¥Á¥È>'))
    snip.add(utf8('¥Á¥È¥ó¥»'), utf8('<¥Á¥È¥ó¥»>'), utf8('</¥Á¥È¥ó¥»>'))
    res = snip.exec(utf8('­À­Ã­Â­Å­Â­Å'))
    assert_equal(1, res.size)
    assert_equal('<¥ê¥á¡¼>­À­Ã</¥ê¥á¡¼><¥Á¥È¥ó¥»>­Â­Å­Â</¥Á¥È¥ó¥»>­Å', euc(res[0]))

  end

  def test_remove_leading_space
    res = Senna::Snip::open({ 'test' => ['<test>', '</test>'],
                              'doc' => ['<doc>', '</doc>'] },
                            10, 1, '', '', nil, nil, Senna::SNIP_SKIP_LEADING_SPACES).exec(
                            'this is test document')
    assert_equal(1, res.size)
    assert_equal([' <test>test</test> <doc>doc</doc>u'], res)

    res = Senna::Snip::open({ 'test' => ['<test>', '</test>'],
                              'doc' => ['<doc>', '</doc>'] },
                            10, 1, '', '', nil, nil, Senna::SNIP_SKIP_LEADING_SPACES).exec(
                            'this is                                                   test                document')
    assert_equal(1, res.size)
    assert_equal(['   <test>test</test>   '], res)
  end
end

