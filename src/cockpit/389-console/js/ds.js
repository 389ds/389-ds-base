var DS_HOME = "/etc/dirsrv/";
var server_id = "None";
var dn_regex = new RegExp( "^([A-Za-z]+=.*)" );



// TODO validation functions

function valid_dn (dn){
  // Validate value is a valid DN (sanity validation)
  var result = dn_regex.test(dn);
  return result;
}

function valid_num (val){
  // Validate value is a number
  return !isNaN(val);
}



function search_dse(){
  document.getElementById('results').innerHTML = "Searching root dse...";
  var cmd = ['/usr/bin/ldapsearch','-Y', 'EXTERNAL' , '-H',
             'ldapi://%2fvar%2frun%2fslapd-localhost.socket',
             '-b' ,'' ,'-s', 'base', 'objectclass=top'];
  cockpit.spawn(cmd, { superuser: true }).done(function(data) {
    $("#results").text(data); }
  );
}

function sort_list (sel) {
  var opts_list = sel.find('option');
  opts_list.sort(function(a, b) { return $(a).text() > $(b).text() ? 1 : -1; });
  sel.html('').append(opts_list);
}

function example() {
  // Example test function
  $("#search").on("click", search_dse);
  //$("#getservers").on("click", get_insts);
}

function set_no_insts () {
    console.log("Settign no instance");
    var select = document.getElementById("select-server");
    var el = document.createElement("option");
    el.textContent = "No instances";
    el.value = "No instances";
    select.appendChild(el);
    select.selectedIndex = "0";
    server_id = "";
}

function get_insts() {
  var insts = [];
  var cmd = ["/bin/sh", "-c", "/usr/bin/ls -d " + DS_HOME + "slapd-*"];

  console.log("Get insts");
  cockpit.spawn(cmd, { superuser: true }).done(function(data) {
    // Parse the output, and skip removed instances and empty lines
    var lines = data.split('\n');
    for (var i = 0; i < lines.length; i++) {
      if (lines[i].endsWith(".removed") == false && lines[i] != "") {
        var serverid = lines[i].replace(DS_HOME, "");
        insts.push(serverid);
      }
    }

    if (server_id != "None") {
      $("#ds-banner").html("Managing Instance <select class=\"btn btn-default dropdown ds-dropdown-server\" id=\"select-server\"></select>");
    }

    // Populate the server instance drop down
    var select = document.getElementById("select-server");
    for(var i = 0; i < insts.length; i++) {
      console.log("Add instance");
      var opt = insts[i];
      var el = document.createElement("option");
      el.textContent = opt;
      el.value = opt;
      select.appendChild(el);
    }
    select.selectedIndex = "0";
    if (insts[0] === undefined) {
      set_no_insts();
    } else {
      server_id = insts[0];
    }

    $("body").show();
  }).fail(function(error){
    set_no_insts();
    $("body").show();
  });
}

$(function() {
  $('#select-server').change(function() {
    server_id = $(this).val();
    $("#ds-banner").html("Managing Instance <select class=\"btn btn-default dropdown ds-dropdown-server\" id=\"select-server\"></select>");
  });
});

$(window.document).ready(function() {

  if(navigator.userAgent.toLowerCase().indexOf('firefoxf') > -1) {  
    $("select@@@").focus( function() {     
      this.style.setProperty( 'outline', 'none', 'important' );
      this.style.setProperty( 'color', 'rgba(0,0,0,0)', 'important' );
      this.style.setProperty( 'text-shadow', '0 0 0 #000', 'important' );
    });
  }
  
});
