/* global initSqlJs */
/* eslint-env worker */
/* eslint no-restricted-globals: ["error"] */

"use strict";

var _dbList = {};

function onModuleReady(SQL) {
    function getDB(name, data) {
        if (_dbList[name] != null) return _dbList[name];
        try {
        	_dbList[name] = new SQL.Database(name, data);
			return _dbList[name];
		}
		catch(err) {
			return err;
		}

    }

    var buff; var data; var result;
    data = this.data;

	var cmds = data.commands;
	var size = cmds.length;

	for(var i=0; i<size; ++i) {
		var cmd = cmds[i];

		if(!cmd.name) {
			throw "command: Missing name string";
		}

		var db = null;
		var res = {};

    	switch(cmd.action) {
        	case "open":
				if(_dbList[cmd.name]) {
					_dbList[cmd.name].close();
					_dbList[cmd.name] = null;
				}

            	buff = cmd.buffer;
            	db = getDB(cmd.name, buff && new Uint8Array(buff));

            	postMessage({
                	id: cmd.id,
                	index: cmd.index,
//					version: cmd.version,
					buffer: cmd.buffer,
					extra: cmd.extra,
					error: typeof db === "string" ? db : null
            	}, [cmd.buffer]);

            	break;

        	case "exec":
        		if(!cmd.sql) {
	                throw "exec: Missing query string";
    	        }

        		db = getDB(cmd.name);

				res = {
                	id: cmd.id,
	                index: cmd.index,
					extra: cmd.extra
        	    };

				try {
					res.results = db.exec(cmd.sql, cmd.params);
					postMessage(res);
				}
				catch(err) {
					postMessage({error: err, commands: [res]});
				}

//            	postMessage(res);

            	break;

	        case "each":
    	        db = getDB(cmd.name);

        	    var callback = function(row) {
            	    return postMessage({
                	    id: cmd.id,
                    	index: cmd.index,
						extra: cmd.extra,
	                    row: row,
    	                finished: false
        	        });
	            };
    	        var done = function() {
        	        return postMessage({
            	        id: data.id,
                	    index: cmd.index,
						extra: cmd.extra,
                    	finished: true
	                });
    	        };

	            db.each(cmd.sql, cmd.params, callback, done);

    	        break;

	        case "export":
    	    	db = getDB(cmd.name);
        	    buff = db.export();
            	result = {
	                id: cmd.id,
    	            index: cmd.index,
					version: cmd.version,
					extra: cmd.extra,
        	        buffer: buff.buffer || buff
	            };
    	        try {
        	        postMessage(result, [buff.buffer || buff]);
            	} catch (error) {
	                postMessage(result);
    	        }

        	    break;

        	case "close":
        		if(_dbList[cmd.name]) {
					_dbList[cmd.name].close();
					_dbList[cmd.name] = null;
				}

            	postMessage({
                	id: cmd.id,
					extra: cmd.extra,
                	index: cmd.index
            	});

            	break;

			case "searchfilter":
				var filter;
				var msg;
				db = getDB(cmd.name);
				if(db) {
					try {
						filter = SQL.FullTextSearch.createSearchFilter("en", cmd.text);
					}
					catch(err) {
						msg = err;
					}
				}
				postMessage({id: "sync", results: filter.slice(), index: cmd.index, extra: cmd.extra, error: msg})

				break;

	        default:
    	        throw new Error("Invalid action : " + cmd.action);
    	}

    }
}

function onError(err) {
    return postMessage({
    	commands: this.data.commands,
//        id: this["data"]["id"],
//        index: this["data"]["index"],
//        extra: this.data.extra,
        error: err["message"]
    });
}

if (typeof importScripts === "function") {
    db = null;
    var sqlModuleReady = initSqlJs();
    self.onmessage = function onmessage(event) {
        return sqlModuleReady
            .then(onModuleReady.bind(event))
            .catch(onError.bind(event));
    };

//	self.filter = function() { console.log("filter")};
}
