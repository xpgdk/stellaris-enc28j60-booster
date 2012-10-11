#!/usr/bin/perl
use Net::Telnet;

$numArgs = $#ARGV + 1;
if($numArgs != 1){
    die( "Usage ./do_flash.pl [main.bin] \n");
}

$file   = $ARGV[0];

$ip = "127.0.0.1"; 
$port = 4444;

$telnet = new Net::Telnet (
    Port   => $port,
    Timeout=>10,
    Errmode=>'die',
    Prompt =>'/>/');

$telnet->open($ip);

print $telnet->cmd('reset halt');
print $telnet->cmd('flash write_image erase '.$file. ' ');
print $telnet->cmd('reset run');
print $telnet->cmd('exit');

print "\n";

