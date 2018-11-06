var DS_HOME = "/etc/dirsrv/";
var server_id = "None";
var server_inst = "";
var dn_regex = new RegExp( "^([A-Za-z]+=.*)" );

/*
 * We can't load the config until all the html pages are load, so we'll use vars
 * to track the loading, and once all the pages are loaded, then we can load the config
 */
var server_page_loaded = 0;
var security_page_loaded = 0;
var db_page_loaded = 0;
var repl_page_loaded = 0;
var plugin_page_loaded = 1;
var schema_page_loaded = 0;
var monitor_page_loaded = 0;
var config_loaded = 0;

// objects to store original values (used for comparing what changed when saving
var config_values = {};
var localpwp_values = {};
var repl_config_values = {};
var repl_cl_values = {};

//TODO - need "config_values" for all the other pages: backend, replication, suffix, etc.

var DSCONF = "dsconf";
var DSCTL = "dsctl";
var DSCREATE = "dscreate";
var ENV = "";

/*
// Used for local development testing
var DSCONF = '/home/mareynol/source/ds389/389-ds-base/src/lib389/cli/dsconf';
var DSCTL = '/home/mareynol/source/ds389/389-ds-base/src/lib389/cli/dsctl';
var DSCREATE = '/home/mareynol/source/ds389/389-ds-base/src/lib389/cli/dscreate';
var ENV = 'PYTHONPATH=/home/mareynol/source/ds389/389-ds-base/src/lib389';
*/

/*
 * Console logging function for CLI commands
 *
 * @param {text}   js_func    The javascript/jquery function that is making this call.
 * @param {text}   desc       The description of the CLI command.
 * @param {array}  cmd_array  An array of all the CLI arguments.
 */
function log_cmd(js_func, desc, cmd_array) {
  if (window.console) {
    var pw_args = ['--passwd', '--bind-pw'];
    var cmd_list = [];
    var converted_pw = false;

    for (var idx in cmd_array) {
      var cmd = cmd_array[idx];
      converted_pw = false;
      for (var arg_idx in pw_args) {
        if ( cmd.startsWith(pw_args[arg_idx]) ) {
          // We are setting a password, if it has a value we need to hide it
          var arg_len = cmd.indexOf('=');
          var arg = cmd.substring(0, arg_len);
          if (cmd.length != arg_len + 1) {
            // We are setting a password value...
            cmd_list.push(arg + "=**********");
            converted_pw = true;
          }
          break;
        }
      }
      if (!converted_pw) {
        cmd_list.push(cmd);
      }
    }
    window.console.log("CMD: " + js_func + ": " + desc + " ==> " + cmd_list.join(' '));
  }
}

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

/*
 * Set the ports numbers on the instance creation form.  If the default ports
 * are taken just unset the values.
 */
function set_ports() {
  var cmd = ['ss', '-ntpl'];
  cockpit.spawn(cmd, { superuser: true, "err": "message"}).done(function(data) {
    var lines = data.split('\n');
    $("#create-inst-port").val("389");
    $("#create-inst-secureport").val("636");
    for (var i = 0; i < lines.length; i++){
      if (lines[i].indexOf("LISTEN") != -1 && lines[i].indexOf(":389 ") != -1) {
        $("#create-inst-port").val("");
      }
      if (lines[i].indexOf("LISTEN") != -1 && lines[i].indexOf(":636 ") != -1) {
        $("#create-inst-secureport").val("");
      }
    }
  });
}

function sort_list (sel) {
  var opts_list = sel.find('option');
  opts_list.sort(function(a, b) { return $(a).text() > $(b).text() ? 1 : -1; });
  sel.html('').append(opts_list);
}


function get_date_string (timestamp) {
  // Convert DS timestamp to a friendly string: 20180921142257Z -> 10/21/2018, 2:22:57 PM
  var year = timestamp.substr(0,4);
  var month = timestamp.substr(4,2);
  var day = timestamp.substr(6,2);
  var hour = timestamp.substr(8,2);
  var minute = timestamp.substr(10,2);
  var sec = timestamp.substr(12,2);
  var date = new Date(parseInt(year), parseInt(month), parseInt(day),
                      parseInt(hour), parseInt(minute), parseInt(sec));

  return date.toLocaleString();
}



function set_no_insts () {
    $("#select-server").empty();
    $("#select-server").append('<option value="No instances">No instances</option>');
    $("#select-server").prop('selectedIndex',0);

    server_id = "";
    server_inst = "";

    $("#server-list-menu").attr('disabled', true);
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#no-instances").show();
}

function check_for_389 () {
  var cmd = ["rpm", "-q", "389-ds-base"];

  cockpit.spawn(cmd, { superuser: true }).fail(function(data) {
    $("#server-list-menu").attr('disabled', true);
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#no-package").show();
  });
}

function check_inst_alive (connect_err) {
  // Check if this instance is started, if not hide configuration pages
  if (connect_err === undefined) {
    connect_err = 0;
  }
  cmd = ['status-dirsrv', server_inst];
  cockpit.spawn(cmd, { superuser: true }).done(function () {
    if (connect_err) {
      $("#ds-navigation").hide();
      $(".all-pages").hide();
      $("#no-connect").show();
    } else {
      // if nav page was hidden reset everything
      if ($("#ds-navigation").is(":hidden") ){
        $(".all-pages").hide();
        $("#ds-navigation").show();
        $("#server-content").show();
        $("#server-config").show();
      }
      $("#not-running").hide();
      $("#no-connect").hide();
    }
  }).fail(function(data) {
    $("#loading-page").hide();
    $("#ds-navigation").hide();
    $(".all-pages").hide();
    $("#not-running").show();
  });
}

function get_insts() {
  // Load initial forms
  $("#server-list-menu").attr('disabled', false);
  $("#ds-navigation").show();
  $(".all-pages").hide();
  $("#no-instances").hide();

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
    $("#select-server").empty();
    for (i = 0; i < insts.length; i++) {
      $("#select-server").append('<option value="' + insts[i] + '">' + insts[i] +'</option>');
    }
    $("#select-server").prop('selectedIndex',0);

    if (insts[0] === undefined) {
      set_no_insts();
      $("#loading-page").hide();
      $("#everything").show();
    } else {
      // We have at least one instance, make sure we "open" the UI
      server_id = insts[0];
      server_inst = insts[0].replace("slapd-", "");
      check_inst_alive();
      load_config();
    }
  }).fail(function(error){
    set_no_insts();
    $("#loading-page").hide();
    $("#everything").show();
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
  check_inst_alive(0);
}

function popup_msg(title, msg) {
  bootpopup({
    title: title,
    content: [
      '<p>' + msg + '</p>'
    ]
  });
}

function popup_confirm(msg, title, callback) {
  if(typeof callback !== "function") {
    callback = function() {};
  }
  var answer = false;
  return bootpopup({
    title: title,
    content: [
      msg
    ],
    showclose: false,
    buttons: ["no", "yes"],
    yes: function() { answer = true; },
    dismiss: function() { callback(answer); },
  });
}

function popup_success(msg) {
  $('#success-msg').html(msg);
  $('#success-form').modal('show');
  setTimeout(function() {$('#success-form').modal('hide');}, 2000);
}

// This is called when any Save button is clicked on the main page.  We call
// all the save functions for all the pages here.  This is not used for modal forms
function save_all () {
  save_config();  // Server Config Page
  //
  // TODO:
  //   save_chaining();
  //   save_chaining_suffix();
  //   save_global_backend();
  //   save_suffix();
  //   save_security();
}

function load_config (){
  // Load the configuration for all the pages.
  var dropdowns = ['local-pwp-suffix', 'select_repl_suffix', 'select-repl-cfg-suffix',
                   'select-repl-agmt-suffix', 'select-repl-winsync-suffix',
                   'cleanallruv-suffix', 'monitor-repl-backend-list'];

  // Show the spinner, and reset the pages
  $("#loading-msg").html("Loading Directory Server configuration for <i><b>" + server_id + "</b></i>...");
  $("#everything").hide();
  $(".all-pages").hide();
  $("#loading-page").show();
  config_loaded = 0;

  /*
   * Start with the dropdowns, if this fails we stop here, otherwise we assume
   * we are up and running and we can load the other config/
   */
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','backend', 'list', '--suffix'];
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    // Update dropdowns
    for (var idx in dropdowns) {
      $("#" + dropdowns[idx]).empty();
    }
    var obj = JSON.parse(data);
    for (var idx in obj['items']) {
      for (var list in dropdowns){
        $("#" + dropdowns[list]).append('<option value="' + obj['items'][idx] + '" selected="selected">' + obj['items'][idx] +'</option>');
      }
    }

    // Server page
    get_and_set_config();
    get_and_set_sasl();
    get_and_set_localpwp();

    // Schema page
    get_and_set_schema_tables();

    // Replication page
    get_and_set_repl_config();
    get_and_set_repl_agmts();
    get_and_set_repl_winsync_agmts();
    get_and_set_cleanallruv();

    // Security page
    // Database page
    // Plugin page
    // Monitoring page

    // Initialize the tabs
    $(".ds-tab-list").css( 'color', '#777');
    $("#server-tab").css( 'color', '#228bc0');

    // Set an interval event to wait for all the pages to load, then show the content
    var loading_config = setInterval(function() {
      if (config_loaded == 1) {
        $("#loading-page").hide();
        $("#everything").show();
        $("#server-content").show();
        $("#server-config").show();
        clearInterval(loading_config);
        console.log("Completed configuration initialization.");
      }
    }, 300);

  }).fail(function(data) {
    popup_err("Failed To Contact Server",data.message);
    $("#everything").show();
    check_inst_alive(1);
  });
}

$(window.document).ready(function() {
  if(navigator.userAgent.toLowerCase().indexOf('firefoxf') > -1) {
    $("select@@@").focus( function() {
      this.style.setProperty( 'outline', 'none', 'important' );
      this.style.setProperty( 'color', 'rgba(0,0,0,0)', 'important' );
      this.style.setProperty( 'text-shadow', '0 0 0 #000', 'important' );
    });
  }
  $("#plugin-tab").on("click", function() {
    $(".all-pages").hide();
    $("#plugin-content").show();
  });
});
