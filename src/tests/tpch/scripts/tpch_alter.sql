-- sccsid:     @(#)dss.ri	2.1.8.1
-- tpcd benchmark version 8.0

-- for table region
alter table region
    add constraint region_pk primary key (r_regionkey);

commit work;

-- for table nation
alter table nation
    add constraint nation_pk primary key (n_nationkey);

commit work;

alter table nation
    add constraint nation_fk1 foreign key (n_regionkey) references region (r_regionkey);

commit work;

-- for table part
alter table part
    add constraint part_pk primary key (p_partkey);

commit work;

-- for table supplier
alter table supplier
    add constraint supplier_pk primary key (s_suppkey);

commit work;

alter table supplier
    add constraint supplier_fk1 foreign key (s_nationkey) references nation (n_nationkey);

commit work;

-- for table partsupp
alter table partsupp
    add constraint partsupp_pk primary key (ps_partkey,ps_suppkey);

commit work;

-- for table customer
alter table customer
    add constraint customer_pk primary key (c_custkey);

commit work;

alter table customer
    add constraint customer_fk1 foreign key (c_nationkey) references nation (n_nationkey);

commit work;

-- for table lineitem
alter table lineitem
    add constraint lineitem_pk primary key (l_orderkey,l_linenumber);

commit work;

-- for table orders
alter table orders
    add constraint orders_pk primary key (o_orderkey);

commit work;

-- for table partsupp
alter table partsupp
    add constraint partsupp_fk1 foreign key (ps_suppkey) references supplier (s_suppkey);

commit work;

alter table partsupp
    add constraint partsupp_fk2 foreign key (ps_partkey) references part (p_partkey);

commit work;

-- for table orders
alter table orders
    add constraint orders_fk1 foreign key (o_custkey) references customer (c_custkey);

commit work;

-- for table lineitem
alter table lineitem
    add constraint lineitem_fk1 foreign key (l_orderkey) references orders (o_orderkey);

commit work;

alter table lineitem
    add constraint lineitem_fk2 foreign key (l_partkey,l_suppkey) references
        partsupp (ps_partkey,ps_suppkey);

commit work;
