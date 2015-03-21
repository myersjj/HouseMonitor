var globalHeatStatus = null;

function HeatStatus() {
	globalHeatStatus = this;
	this.myName = "";

	function createLayout( data, containerName ) {
		var response = eval( data );
		//console.log(dumpObj(response, 'HeatStatus.createLayout', ' ', 5));
		this.myName = response.name;
		var cssPrefix="";
		// get the css prefix to be used
		if( response.css ) {
			cssPrefix=response.css.split( "," )[1];
		}      

		var container=$('#'+containerName );
		container.html("");

		container.append( "<table id=\""+response.name+"tableCont\" cellpadding=\"5px\" border=\"0\" height=\"100%\"></table>" );

		container = $("#"+response.name+"tableCont" );    
		container.append( "<tr><td height=\"1px\" width=\"20%\"></td><td height=\"1px\" width=\"*\"></td></tr>" );
		container.append( "<tr><td height=\"34px\" colspan=\"2\"><div class=\""+cssPrefix+"_title\">"+response.headline+"</div></td></tr>" );
		container.append( "<tr><td align=\"center\" colspan=\"2\">"+
				"<button class=\""+cssPrefix+"_b2 "+cssPrefix+"_button_1 "+cssPrefix+"_mt\" id=\"status_refresh\">Refresh</button>"+
				"</td></tr>"
		);
		container.append( "<tr><td colspan=\"2\"><div id=\""+response.name+"HeatList\" class=\""+cssPrefix+"_text\">"+
				"</div></td></tr>"
		);

		$("."+cssPrefix+"_button_1" ).mouseover(function (event) {
			$(this).css("background-color","#cc6699");
		});
		$("."+cssPrefix+"_button_1" ).mouseout(function () {
			$(this).css("background-color","#cc99cc");
		});
		$("#status_refresh" ).click(function () {
			playSound("images/201.wav");
			propCommand( 'heatstatus', globalHeatStatus );
		});

		this.contentPanel = $("#"+response.name+"HeatList");
		this.contentPanel.append( "<div>&nbsp;</div>" );

		//propCommand( 'currentsong', this );
	}

	function updateContent(resp)
	{
		//console.log(dumpObj(resp, 'HeatStatus.updateContent', ' ', 5));
		// this.contentPanel.html("");
		// Iterate JSON data and display on screen
		if (resp.monitor_data.location_record.length == 0) return;
		var i = 0, lines = [], lid, lname, lstatus, bstatus, nbsp = "&nbsp";
		while (resp.monitor_data.location_record[0][i]) {
			// console.log(dumpObj(data.monitor_data.location_record[0][i],
			// 'location', ' ', 3));
			lid = resp.monitor_data.location_record[0][i].Id;
			lname = resp.monitor_data.location_record[0][i].LocationName;
			lstatus = resp.monitor_data.location_record[0][i].status;
			bstatus = resp.monitor_data.location_record[0][i].bs;
			console.log('location ' + lid +
					'=' + lname + ':' + lstatus + 'bs=' + bstatus);
			lines[i] = "<br/>" + "<input hidden id=\"lid" + lid + "\" value=\"" + lid + "\"/>" +
			"<span>" + lid + ": </span>" +
			"<span>" + lname + ": " + lstatus + "</span>" +
			"<span> Battery: " + bstatus + "</span>";
			console.log('loc html=' + lines[i]);
			this.contentPanel.append( "<div class=\""+this.cssPrefix+"_data\">"+nbsp+lines[i]+"</div>" );
			i++;
		}

		$("."+this.cssPrefix+"_data" ).mouseover(function (event) {
			$(this).css("background-color","#555555");
		});
		$("."+this.cssPrefix+"_data" ).mouseout(function () {
			$(this).css("background-color","#000000");
		});
	}
	
	this.createLayout = createLayout;
	this.updateContent = updateContent;
}
