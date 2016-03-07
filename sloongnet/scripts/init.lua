function require_ex( _mname )
  if package.loaded[_mname] then
    print( string.format("require_ex module[%s] reload", _mname))
  end
  package.loaded[_mname] = nil
  return require( _mname )
end



Init = function( path )
    package.path = path .. 'scripts/?.lua';
    assert(require_ex('comm'),'load comm lua file failed.')
    assert(require_ex('main'),'load main lua file failed.')
    JSON = (assert(loadfile(path .. 'scripts/json.lua')))()
end

OnError = function( msg )
    print('error proc:' .. msg);
end


ProgressMessage = function( uinfo, request, response )
    local jreq = JSON:decode(request)
    local jres = JSON:decode('{}')
    local func = g_all_request_processer[param['funcid']];

    if type(func) == 'function' then
      local code,msg,res = func( uinfo, jreq, jres );
      jres['errno'] = tostring(code);
      jres['errmsg'] = msg or 'success';
    else
      jres['errno'] = "-999"
      jres['errmsg'] = 'not find the processer. the name is %s.' .. jreq['funcid'];
    end
    res = res or -1
    return JSON:encode(jres),res;
end

