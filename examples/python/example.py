import duckdb

import duckdb

con = duckdb.connect(config={'allow_unsigned_extensions' : 'true'})
con.load_extension('geo')

