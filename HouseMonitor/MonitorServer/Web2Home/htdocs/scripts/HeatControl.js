function HeatControl() {
  this.createLayout = createLayout;
  this.updateContent = updateContent;
  function createLayout( data, containerName ) {
    var response = eval( data );
    var cssPrefix="";
    // get the css prefix to be used
    if( response.css ) {
      cssPrefix=response.css.split( "," )[1];
    }      

    var container=$('#'+containerName );
    container.html("");

    container.append( "<table id=\""+response.name+"tableCont\" cellpadding=\"5px\" border=\"0\" height=\"100%\"></table>" );

    container = $("#"+response.name+"tableCont" );    
    container.append( "<tr><td vallign=\"top\" height=\"34px\"><div class=\""+cssPrefix+"_title\">"+response.headline+"</div></td></tr>" );
    container.append( "<tr><td align=\"center\">"+
           "<button class=\""+cssPrefix+"_button\" style=\"font-family:lcarsgtj3,Arial; font-size:28px; width:100px; height:34px; border-width:0px; margin:5px; border-top-right-radius:17px; border-bottom-right-radius:17px; border-top-left-radius:17px; border-bottom-left-radius:17px;\">Set</button>"+
           "<button class=\""+cssPrefix+"_button\" style=\"font-family:lcarsgtj3,Arial; font-size:28px; width:100px; height:34px; border-width:0px; margin:5px; border-top-right-radius:17px; border-bottom-right-radius:17px; border-top-left-radius:17px; border-bottom-left-radius:17px;\">Delete</button>"+
           "<button class=\""+cssPrefix+"_button\" style=\"font-family:lcarsgtj3,Arial; font-size:28px; width:100px; height:34px; border-width:0px; margin:5px; border-top-right-radius:17px; border-bottom-right-radius:17px; border-top-left-radius:17px; border-bottom-left-radius:17px;\">Change</button>"+
           "<button class=\""+cssPrefix+"_button\" style=\"font-family:lcarsgtj3,Arial; font-size:28px; width:100px; height:34px; border-width:0px; margin:5px; border-top-right-radius:17px; border-bottom-right-radius:17px; border-top-left-radius:17px; border-bottom-left-radius:17px;\" onclick=\"propCommand('heatlist');\">Test</button>"+
           "</td></tr>"
    );

    container.append( "<tr><td valign=\"top\" height=\"*\"></td></tr>" );
    $("."+cssPrefix+"_button" ).mouseover(function (event) {
      $(this).css("background-color","#cc6699");
    });
    $("."+cssPrefix+"_button" ).mouseout(function () {
         $(this).css("background-color","#cc99cc");
    });
  }
  
  function updateContent(resp)
  {
	  console.log(dumpObj(resp, 'HeatControl update', ' ', 3));
	  
  }
}