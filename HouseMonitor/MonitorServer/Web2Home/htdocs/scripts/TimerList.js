function TimerList() {
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
    container.append( "<tr><td height=\"34px\"><div class=\""+cssPrefix+"_title\">"+response.headline+"</div></td></tr>" );
    container.append( "<tr><td><div id=\""+response.name+"TimerList\" class=\""+cssPrefix+"_text\">"+
      "</div></td></tr>"
    );
    
    this.contentPanel = $("#"+response.name+"TimerList");
    propCommand( 'tmrlist', this );
  }
  
  function updateContent(resp)
  {
      this.contentPanel.html("");
      var lines = resp.data.split( "\r" );
      var i;
      
      for( i=2; i<lines.length-5; i++ ) {
        var cols=lines[i].indexOf(" ");
        var nbsp="&nbsp";
        if( cols!=1 ) nbsp="";
        
        this.contentPanel.append( "<div class=\""+this.cssPrefix+"_data\">"+nbsp+lines[i].trim()+"</div>" );
      }

   	  $("."+this.cssPrefix+"_data" ).mouseover(function (event) {
        $(this).css("background-color","#555555");
      });
      $("."+this.cssPrefix+"_data" ).mouseout(function () {
           $(this).css("background-color","#000000");
      });
  }
}
