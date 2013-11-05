minetest.error = function(message)
    return message .. "\n" .. debug.traceback()
end
