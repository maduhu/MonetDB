import os, time, sys
from MonetDBtesting import process

def server_start(args = []):
    sys.stderr.write('#mserver\n')
    sys.stderr.flush()
    srv = process.server('sql', args = args, stdin = process.PIPE)
    time.sleep(5)                      # give server time to start
    return srv

def client(file):
    sys.stderr.write('#client\n')
    sys.stderr.flush()
    clt = process.client('sql', stdin = open(file))
    clt.communicate()

def main():
    srv = server_start(["--set", "sql_debug=64"])
    client(os.path.join(os.getenv('RELSRCDIR'),
                        'set_sql_debug_64__breaking_the_DB.SF-1906287_create.sql'))
    srv.communicate()
    srv = server_start()
    client(os.path.join(os.getenv('RELSRCDIR'),
                        'set_sql_debug_64__breaking_the_DB.SF-1906287_drop.sql'))
    srv.communicate()

main()
