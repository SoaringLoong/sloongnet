print('epollproc.lua is lodad')
OnRecvMessage = function( msg )
    local res = string.split(msg,"|");
    local tmp = io.open('tmp.log','w');
    for k,v in pairs(res) do
        print(v);
    end
    tmp.close();
    return 'success';
end
