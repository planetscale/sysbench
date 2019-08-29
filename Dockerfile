FROM debian:stretch-slim
MAINTAINER Planetscale <contact@planetscale.com>

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends gnupg dirmngr ca-certificates && \
    for i in $(seq 1 10); do apt-key adv --no-tty --recv-keys --keyserver ha.pool.sks-keyservers.net 5072E1F5 && break; done && \
    echo 'deb http://repo.mysql.com/apt/debian/ stretch mysql-5.7' > /etc/apt/sources.list.d/mysql.list && \
    apt-get update && apt-get install -y --force-yes --no-install-recommends \
                apt-transport-https ca-certificates \
                pwgen curl gnupg git iputils-ping mysql-client \
		libssl-dev make automake libtool pkg-config libaio-dev \
		libmysqlclient-dev libmysqlclient20 mysql-community-client \
        && rm -rf /var/lib/apt/lists/*
COPY . /src

RUN cd /src \
    && ./autogen.sh \
    && ./configure \
    && make -j \
    && make install

# These are the lua configurations for TPCC with the changes needed to work with
# Vitess.
RUN git clone https://github.com/planetscale/sysbench-tpcc.git /sysbench/sysbench-tpcc

# This is pointing to master as of 2019-08-28, so we know given a particular
# docker image which version we are using.
RUN cd /sysbench/sysbench-tpcc && git checkout a0783efc2dda45e4f0eb765de06a452aa34c46dc

RUN chgrp -R 0 /sysbench && chmod -R g=u /sysbench 

WORKDIR /sysbench

CMD exec /bin/bash -c "trap : TERM INT; sleep infinity & wait"
