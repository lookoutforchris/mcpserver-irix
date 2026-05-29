product mcpserver
	id "MCP Server for IRIX 6.5 0.3.0"
	image sw
		id "MCP Server for IRIX 6.5 Software"
		version 1
		order 0
		subsys base
			id "MCP Server for IRIX 6.5"
			default
			exp "mcpserver.sw.base"
		endsubsys
	endimage
	image man
		id "MCP Server Manual Pages"
		version 1
		order 9999
		subsys manpages
			id "MCP Server Man Pages"
			default
			exp "mcpserver.man.manpages"
		endsubsys
	endimage
endproduct
