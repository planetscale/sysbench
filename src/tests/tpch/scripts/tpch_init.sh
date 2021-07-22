#!/usr/bin/env bash

if [ ! -e tpch-kit ]; then
  git clone https://github.com/gregrahn/tpch-kit
fi

root=$(pwd)
make_args="DATABASE=POSTGRESQL"

if [ `uname` == "Darwin" ]; then
	make_args="$make_args MACHINE=MACOS"
fi

pushd tpch-kit/dbgen

make $make_args

size=1

for sizeFlag in "--size" "-s" ; do
  if [ $1 == $sizeFlag ]; then
    size=$2
  fi
done

mysql_params=""
if [ $3 == "--mysql-params" ]; then
  mysql_params=$4
fi

./dbgen -vf -s "$size"
mkdir "$root/data"
mv *.tbl "$root/data"

popd

mysql "$mysql_params" < tpch_schema.sql

for file in $(find $root/data -name '*.tbl')
do
  table_name=$(echo $file | cut -d'.' -f1 | rev | cut -d'/' -f1 | rev)
  echo "inserting $file for table $table_name"
  echo "SET GLOBAL local_infile=1; /*+ SET_VAR(local_infile=1) */ LOAD DATA LOCAL INFILE '$file' INTO TABLE $table_name FIELDS TERMINATED BY '|' LINES TERMINATED BY '\n';" | mysql --local-infile=1 $mysql_params
  rm -f $file
done

mysql "$mysql_params" < tpch_alter.sql

rmdir "$root/data"