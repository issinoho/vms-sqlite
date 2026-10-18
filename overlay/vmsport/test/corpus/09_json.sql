-- JSON
select json('{"a":1,"b":[1,2,3],"c":{"d":null}}'), json_array(1, 2.5, 'x', null, json('[1]')), json_object('k', 1, 'l', json_array()), json_quote('a"b'), json_valid('{x}'), json_valid('[1,2]');
select json_extract('{"a":{"b":[10,20,{"c":"x"}]}}', '$.a.b[2].c', '$.a.b[#-1]'), '{"a":[1,2]}' -> '$.a', '{"a":[1,2]}' ->> '$.a[1]', '[1,2,3]' -> 2, json_type('{"a":1.5}', '$.a'), json_array_length('[1,[2,3],4]');
select json_set('{"a":1}', '$.b', 2, '$.a', 'x'), json_insert('[1,2]', '$[#]', 3), json_replace('{"a":1}', '$.a', json('[9]')), json_remove('[1,2,3]', '$[1]'), json_patch('{"a":1,"b":2}', '{"b":null,"c":3}');
select key, value, type, fullkey, path from json_tree('{"x":[1,{"y":true}],"z":1e3}') order by id;
select json_group_array(v), json_group_object(k, v) from (select 'k' || value k, value * 1.5 v from json_each('[1,2,3]'));
select hex(jsonb('{"a":[1,2]}')), json(jsonb('[1.25,"x",null]')), json_pretty('{"a":[1,{"b":2}]}'), json_error_position('{"a":}');
select json('[1e308,-0.0,0.1,123456789012345678901234567890,1.5e-10]'), json(' "é😀" ');
