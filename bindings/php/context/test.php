<?php
/* init */
dl('sen_ctx.so');
require 'sen_ctx.phpclass';


/* create DB */

$db_h = Senna_DB::create('/tmp/DBtestchan',0,SEN_ENC_DEFAULT);
$db_h->close();

/* create CTX */

$db_h = Senna_DB::open('/tmp/DBtestchan');
$ctx_h = Senna_CTX::open($db_h);


/* execute queries */

// do query via send & recv
$ctx_h->send('(+ 1 1)');
$req = $ctx_h->recv();
var_dump($req);
/*
   * return array[0] -> 2
   *        array[1] -> NULL(flags of recv)
   */

// do query via send & recv using place-holder
$ctx_h->send('(+ ? 1)',SEN_CTX_MORE);
$ctx_h->send('1');
$req = $ctx_h->recv();
var_dump($req);
/*
   * return array[0] -> 2
   *        array[1] -> NULL(flags of recv)
   */

// do query via exec
$req = $ctx_h->exec('(+ 1 1)');
var_dump($req);
/*
   * return array[0] -> 2
   */

// do query via exec using place-holder (FIFO)
$array = array();
array_push($array,'(+ ? 1)');
array_push($array,'1');
$req = $ctx_h->exec($array);
var_dump($req);
/*
   * return array[0] -> 2
   */


// get status by hash-array
var_dump($ctx_h->info_get());
/*
   * return array['fd']     -> -1
   *        array['status'] -> 0
   *        array['info']   -> 0
   */


/* FIN */
$ctx_h->close();
$db_h->close();
?>
