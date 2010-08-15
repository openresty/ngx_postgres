# vi:filetype=perl

use lib 'lib';
use Test::Nginx::Socket;

repeat_each(2);

plan tests => repeat_each() * blocks();

our $http_config = <<'_EOC_';
    upstream database {
        postgres_server     127.0.0.1:5432 dbname=ngx_test
                            user=ngx_test password=ngx_test;
    }
_EOC_

worker_connections(128);
run_tests();

no_diff();

__DATA__

=== TEST 1: bad query
--- http_config eval: $::http_config
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "i'm bad";
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10



=== TEST 2: wrong credentials
--- http_config
    upstream database {
        postgres_server     127.0.0.1:5432 dbname=ngx_test
                            user=ngx_test password=wrong_pass;
    }
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "update cats set name='bob' where name='bob'";
    }
--- request
GET /postgres
--- error_code: 502
--- timeout: 10



=== TEST 3: no database
--- http_config
    upstream database {
        postgres_server     127.0.0.1:1 dbname=ngx_test
                            user=ngx_test password=ngx_test;
    }
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "update cats set name='bob' where name='bob'";
    }
--- request
GET /postgres
--- error_code: 502
--- timeout: 10



=== TEST 4: multiple queries
--- http_config eval: $::http_config
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "select * from cats; select * from cats";
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10



=== TEST 5: missing query
--- http_config eval: $::http_config
--- config
    location /postgres {
        postgres_pass       database;
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10



=== TEST 6: empty query
--- http_config eval: $::http_config
--- config
    location /postgres {
        set $query          "";
        postgres_pass       database;
        postgres_query      $query;
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10



=== TEST 7: empty pass
--- http_config eval: $::http_config
--- config
    location /postgres {
        set $database       "";
        postgres_pass       $database;
        postgres_query      "update cats set name='bob' where name='bob'";
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10



=== TEST 8: non-existing table
--- http_config eval: $::http_config
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "update table_that_doesnt_exist set name='bob'";
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10