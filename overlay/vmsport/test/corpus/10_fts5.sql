-- FTS5 full-text search
create virtual table docs using fts5(title, body, tokenize = 'porter unicode61');
insert into docs values('VMS', 'OpenVMS is a multi-user operating system with clustering'),
  ('SQLite', 'SQLite is a small fast self-contained SQL database engine'),
  ('Clusters', 'VMS clusters share disks and locks through the distributed lock manager'),
  ('Locks', 'The lock manager grants locks in six modes'),
  ('Files', 'Files on ODS-5 disks keep versions; deleting a file removes one version');
select title from docs where docs match 'lock' order by rank;
select title, round(bm25(docs), 6) from docs where docs match 'vms OR sqlite' order by rank, title;
select title from docs where docs match 'body:version*' order by title;
select highlight(docs, 1, '[', ']') from docs where docs match '"lock manager"' order by rowid;
select snippet(docs, 1, '<', '>', '...', 4) from docs where docs match 'disks' order by rowid;
select title from docs where docs match 'NEAR(lock manager, 2)' order by rowid;
insert into docs(docs) values('optimize'); insert into docs(docs) values('integrity-check');
with recursive c(i) as (select 1 union all select i + 1 from c where i < 3000) insert into docs select 'doc' || i, 'word' || (i % 37) || ' common term ' || (i % 101) from c;
select count(*) from docs where docs match 'common'; select count(*) from docs where docs match 'word5 AND term'; select min(rowid), max(rowid) from docs where docs match 'word36';
