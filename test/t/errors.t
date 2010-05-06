# vi:filetype=perl

use lib 'lib';
use Test::Nginx::Socket;

repeat_each(2);

plan tests => repeat_each() * blocks();

worker_connections(128);
run_tests();

no_diff();

__DATA__

=== TEST 1: bad query
little-endian systems only

--- http_config
    upstream database {
        postgres_server     127.0.0.1 dbname=test
                            user=monty password=some_pass;
    }
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "update table_that_doesnt_exist set name='bob'";
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10



=== TEST 2: wrong credentials
little-endian systems only

--- http_config
    upstream database {
        postgres_server     127.0.0.1 dbname=test
                            user=monty password=wrong_pass;
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
little-endian systems only

--- http_config
    upstream database {
        postgres_server     127.0.0.1:1 dbname=test
                            user=monty password=some_pass;
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
little-endian systems only

--- http_config
    upstream database {
        postgres_server     127.0.0.1 dbname=test
                            user=monty password=some_pass;
    }
--- config
    location /postgres {
        postgres_pass       database;
        postgres_query      "select * from cats; select * from cats";
    }
--- request
GET /postgres
--- error_code: 500
--- timeout: 10