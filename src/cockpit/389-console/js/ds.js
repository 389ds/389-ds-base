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

function tableize (val) {
  // Truncate a long value to fit inside table
  if (val.length > 25){
    val = val.substring(0,25) + "...";
  }
  return val;
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

// POC - REMOVE!!!!!
function test_json_and_dsconf () {
    var cmd = ['/home/mareynol/source/ds389/389-ds-base/src/lib389/cli/dsconf',
               '-j', 'ldapi://%2fvar%2frun%2fslapd-localhost.socket','backend',
               'list']
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": ["PYTHONPATH=/home/mareynol/source/ds389/389-ds-base/src/lib389"]}).done(function(data) {
        var obj = JSON.parse(data);
        console.log("backend: " + obj['items']);
    }).fail(function(data) {
        console.log("failed: " + data.message);
    });
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

    $("#server-list-menu").attr('disabled', true);
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#no-instances").show();
}

function check_for_389 () {
  var cmd = ["ls", "/etc/dirsrv"];

  cockpit.spawn(cmd, { superuser: true }).fail(function(data) {
    $("#server-list-menu").attr('disabled', true);
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#no-package").show();
  });
}

function get_insts() {
  var insts = [];
  var cmd = ["/bin/sh", "-c", "/usr/bin/ls -d " + DS_HOME + "slapd-*"];

  cockpit.spawn(cmd, { superuser: true }).done(function(data) {
    // Parse the output, and skip removed instances and empty lines
    var lines = data.split('\n');
    var i = 0;
    for (i = 0; i < lines.length; i++) {
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
    var list_length = select.options.length;

    // Clear the list first
    for (i = 0; i < list_length; i++) {
      select.options[i] = null;
    }

    // Update the list
    for (i = 0; i < insts.length; i++) {
      console.log("Add instance: " + insts[i]);
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
      // We have at least one instance, make sure we "open" the UI
      $("#server-list-menu").attr('disabled', false);
      $("#ds-navigation").show();
      $(".all-pages").hide();
      $("#no-instances").hide();
      $("#server-content").show();
      $("#server-config").show();
      server_id = insts[0];
    }

    $("body").show();
  }).fail(function(error){
    set_no_insts();
    $("body").show();
  });
}

function report_err( input, msg) {
  $(".ds-modal-error").html('Error: ' + msg);
  input.css("border-color", "red");
  $(".ds-modal-error").show();
}


function popup_err(title, msg) {
  // Display errors from the cli (we have to use pre tags)
  bootpopup({
    title: title,
    content: [
      '<pre>' + msg + '</pre>'
    ]
  });
}

function popup_msg(title, msg) {
  bootpopup({
    title: title,
    content: [
      '<p>' + msg + '</p>'
    ]
  });
}

$(function() {
  $('#select-server').change(function() {
    server_id = $(this).val();
    // TODO - reload config - do everything!!!!!
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
