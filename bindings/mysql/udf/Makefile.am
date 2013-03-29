lib_LTLIBRARIES = libmysql_udf_snippet.la
libmysql_udf_snippet_la_CFLAGS = @CFLAGS@
libmysql_udf_snippet_la_LDFLAGS = -module -avoid-version -no-undefined
libmysql_udf_snippet_la_SOURCES = udf_snippet.c

FUNCTION_NAME = snippet

load :
	echo "CREATE FUNCTION $(FUNCTION_NAME) RETURNS STRING SONAME \"libmysql_udf_snippet.so\";" | mysql -f -u root -p mysql

drop : 
	echo "DROP FUNCTION $(FUNCTION_NAME);" | mysql -f -u root -p mysql

