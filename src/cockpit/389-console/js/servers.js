
var sasl_action_html =
  '<div class="dropdown">' +
    '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="dropdownMenu1" data-toggle="dropdown">' +
      'Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="dropdownMenu1">' +
      '<li role=""><a role="menuitem" class="sasl-edit-btn" tabindex="2" href="#">Edit Mapping</a></li>' +
      '<li role=""><a role="menuitem" class="sasl-del-btn" tabindex="1" href="#">Delete Mapping</a></li>' +
    '</ul>' +
  '</div>';

var local_pwp_html =
  '<div class="dropdown" >' +
     '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="menu1" data-toggle="dropdown">Choose Action...' +
       '<span class="caret"></span></button>' +
     '<ul id="test-drop" class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="menu1">' +
       '<li role="policy-role"><a role="pwpolicy" tabindex="0" class="edit-local-pwp" href="#">View/Edit Policy</a></li>' +
       '<li role="policy-role"><a role="pwpolicy" tabindex="-1" class="delete-local-pwp" href="#">Delete Policy</a></li>' +
     '</ul>' +
   '</div>';

var create_full_template =
  "[general]\n" +
  "config_version = 2\n" +
  "defaults = 999999999\n" +
  "full_machine_name = FQDN\n" +
  "selinux = True\n" +
  "strict_host_checking = True\n" +
  "systemd = True\n" +
  "[slapd]\n" +
  "backup_dir = /var/lib/dirsrv/slapd-{instance_name}/bak\n" +
  "bin_dir = /usr/bin\n" +
  "cert_dir = /etc/dirsrv/slapd-{instance_name}\n" +
  "config_dir = /etc/dirsrv/slapd-{instance_name}\n" +
  "data_dir = /usr/share\n" +
  "db_dir = /var/lib/dirsrv/slapd-{instance_name}/db\n" +
  "user = dirsrv\n" +
  "group = dirsrv\n" +
  "initconfig_dir = /etc/sysconfig\n" +
  "inst_dir = /usr/lib64/dirsrv/slapd-{instance_name}\n" +
  "instance_name = localhost\n" +
  "ldif_dir = /var/lib/dirsrv/slapd-{instance_name}/ldif\n" +
  "lib_dir = /usr/lib64\n" +
  "local_state_dir = /var\n" +
  "lock_dir = /var/lock/dirsrv/slapd-{instance_name}\n" +
  "log_dir = /var/log/dirsrv/slapd-{instance_name}\n" +
  "port = PORT\n" +
  "prefix = /usr\n" +
  "root_dn = ROOTDN\n" +
  "root_password = ROOTPW\n" +
  "run_dir = /var/run/dirsrv\n" +
  "sbin_dir = /usr/sbin\n" +
  "schema_dir = /etc/dirsrv/slapd-{instance_name}/schema\n" +
  "secure_port = SECURE_PORT\n" +
  "self_sign_cert = True\n" +
  "sysconf_dir = /etc\n" +
  "tmp_dir = /tmp\n";

var create_inf_template =
  "[general]\n" +
  "config_version = 2\n" +
  "full_machine_name = FQDN\n\n" +
  "[slapd]\n" +
  "user = dirsrv\n" +
  "group = dirsrv\n" +
  "instance_name = INST_NAME\n" +
  "port = PORT\n" +
  "root_dn = ROOTDN\n" +
  "root_password = ROOTPW\n" +
  "secure_port = SECURE_PORT\n" +
  "self_sign_cert = SELF_SIGN\n";


var sasl_table;
var pwp_table;

// log levels
var accesslog_levels = [4, 256, 512]
var errorlog_levels =  [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 2048,
                        4096, 8192, 16384, 32768, 65536, 262144, 1048576];

function load_server_config() {
  var mark = document.getElementById("server-config-title");
  mark.innerHTML = "Configuration for server: <b>" + server_id + "</b>";
}

function clear_sasl_map_form () {
  $(".ds-modal-error").hide();
  $(".sasl-input").val("");
  $(".sasl-input").css("border-color", "initial");
}

function clear_local_pwp_form () {
  $(".ds-pwp-input").val("");
  $(".ds-pwp-checkbox").prop('checked', false);
  $("#local-passwordstoragescheme").prop('selectedIndex',0);
  $("#subtree-pwp-radio").attr('disabled', false);
  $("#user-pwp-radio").attr('disabled', false);
  $("#local-entry-dn").attr('disabled', false);
  $("#local-pwp-header").html("<b>Create Local Password Policy</b>");
}

function clear_inst_input() {
  // Reset the color of the fields
  $(".ds-inst-input").css("border-color", "initial");
}

function clear_inst_form() {
  $(".ds-modal-error").hide();
  $("#create-inst-serverid").val("");
  $("#create-inst-port").val("389");
  $("#create-inst-secureport").val("636");
  $("#create-inst-rootdn").val("cn=Directory Manager");
  $("#rootdn-pw").val("");
  $("#rootdn-pw-confirm").val("");
  $("#backend-suffix").val("");
  $("#backend-name").val("");
  $("#create-sample-entries").prop('checked', false);
  $("#create-inst-tls").prop('checked', true);
  $(".ds-inst-input").css("border-color", "initial");
}

/*
 * Validate the val and add it to the argument list for "dsconf"
 *
 * arg_list - array of options for dsconf
 * valtype - value type:  "num" or "dn"
 * val - the new value
 * def_val - set this default is there is no new value("")
 * edit - if we are editing a value, we accept ("") and do not ignore it
 * attr - the dict key(its also the element ID)
 * arg - the CLI argument (--pwdlen)
 * msg - error message to display when things go wrong
 *
 * Return false on validation failure
 */
function add_validate_arg (arg_list, valtype, val, def_val, edit, attr, arg, msg) {
  if ( val != "" || (edit && localpwp_values[attr] !== undefined && val != localpwp_values[attr])) {
    if (val == "") {
       val = def_val;
    }
    if ( valtype == "num" && !valid_num(val) ) {
      popup_msg("Error", "\"" + msg + "\" value \"" + val + "\" is not a number");
      return false;
    } else if (valtype == "dn" && !valid_dn(val) && val != "" ) {
      popup_msg("Error", "\"" + msg + "\" value \"" + val + "\" is not a DN (distinguished name)");
      return false;
    }
    arg_list.push(arg + '=' + val);
    return true;
  }
  // No change, no error
  return true;
}

function get_and_set_config () {
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','config', 'get'];
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    // Reset tables before populating them
    $(".ds-accesslog-table").prop('checked', false);
    $(".ds-errorlog-table").prop('checked', false);

    for (var attr in obj['attrs']) {
      var val = obj['attrs'][attr][0];
      attr = attr.toLowerCase();
      if( $('#' + attr).length ) {
        // We have  an element that matches, set the html and store the original value
        $("#" + attr).val(val);  // Always set value, then check if its something else
        if (val == "on") {
          $("#" + attr).prop('checked', true);
          $("#" + attr).trigger('change');
        } else if (val == "off") {
          $("#" + attr).prop('checked', false);
          $("#" + attr).trigger('change');
        }
        config_values[attr] = val;
      }

      // Do the log level tables
      if (attr == "nsslapd-accesslog-level") {
        var level_val = parseInt(val);
        for ( var level in accesslog_levels ) {
          if (level_val & accesslog_levels[level]) {
            $("#accesslog-" + accesslog_levels[level].toString()).prop('checked', true);
          }
        }
      } else if (attr == "nsslapd-errorlog-level") {
        var level_val = parseInt(val);
        for ( var level in errorlog_levels ) {
          if (level_val & errorlog_levels[level]) {
            $("#errorlog-" + errorlog_levels[level].toString()).prop('checked', true);
          }
        }
      }
    }
    check_inst_alive();
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive(1);
  });
}

function update_suffix_dropdowns () {
    var dropdowns = ['local-pwp-suffix', 'select_repl_suffix', 'select-repl-cfg-suffix',
                     'select-repl-agmt-suffix', 'select-repl-winsync-suffix',
                     'monitor-repl-backend-list'];

    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','backend', 'list', '--suffix'];
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      // Clear all the dropdowns first
      for (var idx in dropdowns) {
        $("#" + dropdowns[idx]).empty();
      }
      // Update dropdowns
      var obj = JSON.parse(data);
      for (var idx in obj['items']) {
        for (var list in dropdowns){
          $("#" + dropdowns[list]).append('<option value="' + obj['items'][idx] + '" selected="selected">' + obj['items'][idx] +'</option>');
        }
      }
  });
}

function get_and_set_localpwp () {
  // Now populate the table
  var suffix = $('#local-pwp-suffix').val();
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','localpwp', 'list', suffix ];
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    // Empty table
    pwp_table.clear().draw();

    // Populate table
    for (var idx in obj['items']) {
      pwp_table.row.add([
        obj['items'][idx][0],
        obj['items'][idx][1],
        local_pwp_html,]
      ).draw( false );
    }
  });
}

function get_and_set_sasl () {
  // First empty the table
  sasl_table.clear().draw();

  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','sasl', 'list'];
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
    for (var idx in obj['items']) {
      var map_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','sasl', 'get', obj['items'][idx] ];
      cockpit.spawn(map_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        var map_obj = JSON.parse(data);

        // Update html table
        var sasl_priority = '100';
        if ( map_obj['attrs'].hasOwnProperty('nssaslmappriority') ){
          sasl_priority = map_obj['attrs'].nssaslmappriority
        }
        sasl_table.row.add( [
          map_obj['attrs'].cn,
          map_obj['attrs'].nssaslmapregexstring,
          map_obj['attrs'].nssaslmapbasedntemplate,
          map_obj['attrs'].nssaslmapfiltertemplate,
          sasl_priority,
          sasl_action_html
        ] ).draw( false );
      });
    }
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive(1);
  });
}

function apply_mods(mods) {
  var mod = mods.pop();

  if (!mod){
    popup_success("Successfully updated configuration");
    return; /* all done*/
  }
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','config', 'replace'];
  cmd.push(mod.attr + "=" + mod.val);

  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).then(function() {
    config_values[mod.attr] = mod.val;
    // Continue with next mods (if any))
    apply_mods(mods);
  }, function(ex) {
     popup_err("Error", "Failed to update attribute: " + mod.attr + "\n\n" +  ex);
     // Reset HTML for remaining values that have not been processed
     $("#" + mod.attr).val(config_values[mod.attr]);
     for (remaining in mods) {
       $("#" + remaining.attr).val(config_values[remaining.attr]);
     }
     check_inst_alive(1);
     return;  // Stop on error
  });
}

function save_config() {
  // Loop over current config_values check for differences
  var mod_list = [];

  for (var attr in config_values) {
    var mod = {};
    if ( $("#" + attr).is(':checkbox')) {
      // Handle check boxes
      if ( $("#" + attr).is(":checked")) {
        if (config_values[attr] != "on") {
          mod['attr'] = attr;
          mod['val'] = "on";
          mod_list.push(mod);
        }
      } else {
        // Not checked
        if (config_values[attr] != "off") {
          mod['attr'] = attr;
          mod['val'] = "off";
          mod_list.push(mod);
        }
      }
    } else {
      // Normal input
      var val = $("#" + attr).val();

      // But first check for rootdn-pw changes and check confirm input matches
      if (attr == "nsslapd-rootpw" && val != config_values[attr]) {
        // Password change, make sure passwords match
        if (val != $("#nsslapd-rootpw-confirm").val()){
          popup_msg("Passwords do not match!", "The Directory Manager passwords do not match, please correct before saving again.");
          return;
        }
      }

      if ( val && val != config_values[attr]) {
        mod['attr'] = attr;
        mod['val'] = val;
        mod_list.push(mod);
      }
    }
  }

  // Save access log levels
  var access_log_level = 0;
  $(".ds-accesslog-table").each(function() {
    var val = this.id;
    if (this.checked){
      val = parseInt(val.replace("accesslog-", ""));
      access_log_level += val;
    }
  });
  mod['attr'] = "nsslapd-accesslog-level";
  mod['val'] = access_log_level;
  mod_list.push(mod);

  // Save error log levels
  var error_log_level = 0;
  $(".ds-errorlog-table").each(function() {
    var val = this.id;
    if (this.checked) {
      val = parseInt(val.replace("errorlog-", ""));
      error_log_level += val;
    }
  });
  mod['attr'] = "nsslapd-errorlog-level";
  mod['val'] = error_log_level;
  mod_list.push(mod);

  // Build dsconf commands to apply all the mods
  if (mod_list.length) {
    apply_mods(mod_list);
  } else {
    // No changes to save, log msg?  popup_msg()
  }
}

// load the server config pages
$(document).ready( function() {

  // Set an interval event to wait for all the pages to load, then load the config
  var init_config = setInterval(function() {
      if (server_page_loaded == 1 && security_page_loaded == 1 && db_page_loaded == 1 &&
          repl_page_loaded == 1 && schema_page_loaded == 1 && plugin_page_loaded == 1 &&
          monitor_page_loaded == 1)
      {
        get_insts();
        console.log("Loaded configuration.");
        clearInterval(init_config);
      }
  }, 200);

  $("#main-banner").load("banner.html");
  check_for_389();

  $("#server-tab").css( 'color', '#228bc0');

  $("#server-content").load("servers.html", function () {
    // Initialize all the tables first
    sasl_table = $('#sasl-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No SASL Mappings",
        "search": "Search Mappings"
      },
      "columnDefs": [ {
        "targets": 5,
        "orderable": false
      } ]
    });

    // backup/restore table
    var backup_table = $('#backup-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No backups available for restore",
        "search": "Search Backups"
      },
      "columnDefs": [ {
        "targets": [3, 4],
        "orderable": false
      } ],
      "columns": [
        { "width": "120px" },
        { "width": "80px" },
        { "width": "30px" },
        { "width": "40px" },
        { "width": "30px" }
      ],
    });

    // Set up local passwd policy table
    pwp_table = $('#passwd-policy-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No local policies",
        "search": "Search Policies"
      },
      "columnDefs": [ {
        "targets": 2,
        "orderable": false
      } ]
    });

    // Handle changing instance here
    $('#select-server').on("change", function() {
      server_id = $(this).val();
      server_inst = server_id.replace("slapd-", "");
      load_config();
    });

    $('.disk-monitoring').hide();
    $(".all-pages").hide();
    $("#server-content").show();
    $("#server-config").show();

    // To remove text border on firefox on dropdowns)
    if(navigator.userAgent.toLowerCase().indexOf('firefox') > -1) {
      $("select").focus( function() {
        this.style.setProperty( 'outline', 'none', 'important' );
        this.style.setProperty( 'color', 'rgba(0,0,0,0)', 'important' );
        this.style.setProperty( 'text-shadow', '0 0 0 #000', 'important' );
      });
    }

    $(".save-button").on('click', function (){
      // This is for all pages.  Click Save -> it saves everything
      save_all();
    });

    // Events
    $(".ds-nav-choice").on('click', function (){
      $(".ds-tab-list").css( 'color', '#777');
      var tab = $(this).attr("parent-id");
      $("#" + tab).css( 'color', '#228bc0');
    });

    $(".ds-tab-standalone").on('click', function (){
      $(".ds-tab-list").css( 'color', '#777');
      $(this).css( 'color', '#228bc0');
    });

    $("#server-config-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-config").show();
    });
    $("#server-sasl-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-sasl").show();
    });
    $("#server-gbl-pwp-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#global-password-policy").show();
    });
    $("#server-local-pwp-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#local-password-policy").show();
    });
    $("#server-log-access-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-access-log").show();
    });
    $("#server-log-audit-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-audit-log").show();
    });
    $("#server-log-auditfail-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-auditfail-log").show();
    });
    $("#server-log-errors-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-errors-log").show();
    });
    $("#server-tasks-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-tasks").show();
    });
    $("#server-tuning-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-tuning").show();
    });
    $("#server-ldapi-btn").on("click", function() {
      $(".all-pages").hide();
      $("#server-content").show();
      $("#server-ldapi").show();
    });

    // Disable disk monitoring input if not in use
    $("#nsslapd-disk-monitoring").change(function() {
      if(this.checked) {
        $('.disk-monitoring').show();
      } else {
        $('.disk-monitoring').hide();
      }
    });

    $('.ds-loglevel-table tr').click(function(event) {
        if (event.target.type !== 'checkbox') {
            $(':checkbox', this).trigger('click');
        }
    });

    $("#create-sasl-map-btn").on("click", function () {
      clear_sasl_map_form();
      $("#sasl-map-name").prop("readonly", false);
    });

    $("#test-sasl-regex").change(function() {
      if(this.checked) {
        // Test SASL mapping
        $("#sasl-test-div").show();
      } else {
        $("#sasl-test-div").hide();
      }
    });

    // Test SASL Mapping Regex
    $("#sasl-test-regex-btn").on('click', function () {
      var result = "No match!"
      var regex = $("#sasl-map-regex").val().replace(/\\\(/g, '(').replace(/\\\)/g, ')');
      var test_string = $("#sasl-test-regex-string").val();
      var sasl_regex = RegExp(regex);
      if (sasl_regex.test(test_string)){
        popup_msg("Match", "The text matches the regular expression");
      } else {
        popup_msg("No Match", "The text does not match the regular expression");
      }
    });

    // Edit SASL mapping
    $(document).on('click', '.sasl-edit-btn', function(e) {
      // Load the Edit form
      e.preventDefault();
      clear_sasl_map_form();
      var data = sasl_table.row( $(this).parents('tr') ).data();
      var edit_sasl_name = data[0];
      var edit_sasl_regex = data[1];
      var edit_sasl_base = data[2];
      var edit_sasl_filter = data[3];
      var edit_sasl_priority = data[4];

      $("#sasl-header").html("Edit SASL Mapping");
      $("#sasl-map-name").val(edit_sasl_name);
      $("#sasl-map-name").prop("readonly", true);
      $("#sasl-map-regex").val(edit_sasl_regex);
      $("#sasl-map-base").val(edit_sasl_base);
      $("#sasl-map-filter").val(edit_sasl_filter);
      $("#sasl-map-priority").val(edit_sasl_priority);
      $("#sasl-map-form").modal("toggle");
    });

    // Verify SASL Mapping regex - open modal and ask for "login" to test regex mapping
    $(document).on('click', '.sasl-verify-btn', function(e) {
        // TODO - get this working
        e.preventDefault();
        var data = sasl_table.row( $(this).parents('tr') ).data();
        var verify_sasl_name = data[0];
    });

    // Delete SASL Mapping
    $(document).on('click', '.sasl-del-btn', function(e) {
        e.preventDefault();
        var data = sasl_table.row( $(this).parents('tr') ).data();
        var del_sasl_name = data[0];
        var sasl_row = $(this); // Store element for callback
        popup_confirm("Are you sure you want to delete sasl mapping: <b>" + del_sasl_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','sasl', 'delete', del_sasl_name];
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            sasl_table.row( sasl_row.parents('tr') ).remove().draw( false );
            popup_success("Removed SASL mapping <b>" + del_sasl_name + "</b>");
          }).fail(function(data) {
            popup_err("Failure Deleting SASL Mapping",
                      "Failed To Delete SASL Mapping: <b>" + del_sasl_name + "</b>: \n" + data.message);
          });
        }
      });
    });

    // Load password policy and update form based on settings
    // TODO

    // Global password policy form control
    $("#passwordhistory").change(function() {
      if(this.checked) {
        $('#passwordinhistory').attr('disabled', false);
      } else {
        $('#passwordinhistory').attr('disabled', true);
      }
    });
    if ( $("#passwordhistory").is(":checked") ) {
      $('#passwordinhistory *').attr('disabled', false);
    } else {
      $('#passwordinhistory *').attr('disabled', true);
    }

    $("#passwordexp").change(function() {
      if(this.checked) {
        $('#expiration-attrs *').attr('disabled', false);
      } else {
        $('#expiration-attrs *').attr('disabled', true);
      }
    });
    if ( $("#passwordexp").is(":checked") ) {
      $('#expiration-attrs *').attr('disabled', false);
    } else {
      $('#expiration-attrs *').attr('disabled', true);
    }

    $("#passwordchecksyntax").change(function() {
      if(this.checked) {
        $('#syntax-attrs *').attr('disabled', false);
      } else {
        $('#syntax-attrs *').attr('disabled', true);
      }
    });
    if ( $("#passwordchecksyntax").is(":checked") ) {
      $('#syntax-attrs *').attr('disabled', false);
    } else {
      $('#syntax-attrs *').attr('disabled', true);
    }

    $("#passwordlockout").change(function() {
      if(this.checked) {
        $('#lockout-attrs *').attr('disabled', false);
      } else {
        $('#lockout-attrs *').attr('disabled', true);
      }
    });
    if ( $("#passwordlockout").is(":checked") ) {
      $('#lockout-attrs *').attr('disabled', false);
    } else {
      $('#lockout-attrs *').attr('disabled', true);
    }

    /*
     * local password policy form control
     */
    $("#local-passwordhistory").change(function() {
      if(this.checked) {
        $('#local-passwordinhistory').attr('disabled', false);
      } else {
        $('#local-passwordinhistory').attr('disabled', true);
      }
    });
    if ( $("#local-passwordhistory").is(":checked") ) {
      $('#local-passwordhistory *').attr('disabled', false);
    } else {
      $('#local-passwordhistorys *').attr('disabled', true);
    }

    $("#local-passwordexp").change(function() {
      if(this.checked) {
        $('#local-expiration-attrs *').attr('disabled', false);
      } else {
        $('#local-expiration-attrs *').attr('disabled', true);
      }
    });
    if ( $("#local-passwordexp").is(":checked") ) {
      $('#local-expiration-attrs *').attr('disabled', false);
    } else {
      $('#local-expiration-attrs *').attr('disabled', true);
    }

    $("#local-passwordchecksyntax").change(function() {
      if(this.checked) {
        $('#local-syntax-attrs *').attr('disabled', false);
      } else {
        $('#local-syntax-attrs *').attr('disabled', true);
      }
    });
    if ( $("#local-passwordchecksyntax").is(":checked") ) {
      $('#local-syntax-attrs *').attr('disabled', false);
    } else {
      $('#local-syntax-attrs *').attr('disabled', true);
    }

    $("#local-passwordlockout").change(function() {
      if(this.checked) {
        $('#local-lockout-attrs *').attr('disabled', false);
      } else {
        $('#local-lockout-attrs *').attr('disabled', true);
      }
    });
    if ( $("#local-passwordlockout").is(":checked") ) {
      $('local-lockout-attrs *').attr('disabled', false);
    } else {
      $('#local-lockout-attrs *').attr('disabled', true);
    }

    /*
     * Logging form control
     */
    $("#nsslapd-accesslog-logging-enabled").change(function() {
      if(this.checked) {
        $('#accesslog-attrs *').attr('disabled', false);
      } else {
        $('#accesslog-attrs *').attr('disabled', true);
      }
    });

    $("#nsslapd-errorlog-logging-enabled").change(function() {
      if(this.checked) {
        $('#errorlog-attrs *').attr('disabled', false);
      } else {
        $('#errorlog-attrs *').attr('disabled', true);
      }
    });

    $("#nsslapd-auditlog-logging-enabled").change(function() {
      if(this.checked) {
        $('#auditlog-attrs *').attr('disabled', false);
      } else {
        $('#auditlog-attrs *').attr('disabled', true);
      }
    });

    $("#nsslapd-auditfaillog-logging-enabled").change(function() {
      if(this.checked) {
        $('#auditfaillog-attrs *').attr('disabled', false);
      } else {
        $('#auditfaillog-attrs *').attr('disabled', true);
      }
    });

    $("#nsslapd-ndn-cache-enabled").change(function() {
      if(this.checked) {
        $('#nsslapd-ndn-cache-max-size').attr('disabled', false);
      } else {
        $('#nsslapd-ndn-cache-max-size').attr('disabled', true);
      }
    });

    // LDAPI form control
    $("#nsslapd-ldapilisten").change(function() {
      if(this.checked) {
        $('.ldapi-attrs').show();
        if ( $("#nsslapd-ldapiautobind").is(":checked") ){
          $(".autobind-attrs").show();
          if ( $("#nsslapd-ldapimaptoentries").is(":checked") ){
            $(".autobind-entry-attrs").show();
          } else {
            $(".autobind-entry-attrs").hide();
          }
        } else {
           $(".autobind-attrs").hide();
           $(".autobind-entry-attrs").hide();
           $("#nsslapd-ldapimaptoentries").prop("checked", false );
        }
      } else {
        $('.ldapi-attrs').hide();
        $(".autobind-attrs").hide();
        $(".autobind-entry-attrs").hide();
        $("#nsslapd-ldapiautobind").prop("checked", false );
        $("#nsslapd-ldapimaptoentries").prop("checked", false );
      }
    });

    $("#nsslapd-ldapiautobind").change(function() {
      if (this.checked){
        $(".autobind-attrs").show();
        if ( $("#nsslapd-ldapimaptoentries").is(":checked") ){
          $(".autobind-entry-attrs").show();
        } else {
          $(".autobind-entry-attrs").hide();
        }
      } else {
        $(".autobind-attrs").hide();
        $(".autobind-entry-attrs").hide();
        $("#nsslapd-ldapimaptoentries").prop("checked", false );
      }
    });

    $("#nsslapd-ldapimaptoentries").change(function() {
      if (this.checked){
        $(".autobind-entry-attrs").show();
      } else {
        $(".autobind-entry-attrs").hide();
      }
    });

    /*
     *  Modal Forms
     */

    /*
     * Local password policy
     */
    $("#create-local-pwp-btn").on("click", function () {
      clear_local_pwp_form();
    });

    $("#local-pwp-save").on("click", function() {
      /*
       * We are either saving a new policy or editing an existing one
       * If we are editing and we remove a setting, we have to set it
       * to the default value - so it makes things a little more tedious
       * for editing a local policy
       */

      // Is this the create or edit form?
      var edit = false;
      if ( $("#local-pwp-header").text().startsWith('Edit') ) {
        edit = true;
      }

      /*
       * Get all the current values from the form.
       */
      var policy_name = $("#local-entry-dn").val();
      var pwp_track = "off";
      if ( $("#local-passwordtrackupdatetime").is(":checked") ) {
        pwp_track = "on";
      }
      var pwp_passwordchange = "off";
      if ($("#local-passwordchange").is(":checked") ){
        pwp_passwordchange = "on";
      }
      var pwp_passwordmustchange = "off";
      if ($("#local-passwordmustchange").is(":checked") ){
        pwp_passwordmustchange = "on";
      }
      var pwp_history = "off";
      if ($("#local-passwordhistory").is(":checked") ){
        pwp_history = "on";
      }
      var pwp_exp = "off";
      if ($("#local-passwordexp").is(":checked") ){
        pwp_exp = "on";
      }
      var pwp_sendexp = "off";
      if ($("#local-passwordsendexpiringtime").is(":checked") ){
        pwp_sendexp = "on";
      }
      var pwp_lockout = "off";
      if ($("#local-passwordlockout").is(":checked") ){
        pwp_lockout = "on";
      }
      var pwp_unlock = "off";
      if ($("#local-passwordunlock").is(":checked") ){
        pwp_unlock = "on";
      }
      var pwp_checksyntax = "off";
      if ($("#local-passwordchecksyntax").is(":checked") ){
        pwp_checksyntax = "on";
      }
      var pwp_palindrome = "on";
      if ( $("#local-passwordpalindrome").is(":checked") ){
        pwp_palindrome = "off";
      }
      var pwp_dictcheck = "off";
      if ( $("#local-passworddictcheck").is(":checked") ){
         pwp_dictcheck = "on";
      }

      var pwp_admin = $("#local-passwordadmindn").val();
      var pwp_inhistory = $("#local-passwordinhistory").val();
      var pwp_minage = $("#local-passwordminage").val();
      var pwp_maxage = $("#local-passwordmaxage").val();
      var pwp_gracelimit = $("#local-passwordgracelimit").val();
      var pwp_warning = $("#local-passwordwarning").val();
      var pwp_maxfailure = $("#local-passwordmaxfailure").val();
      var pwp_failcount = $("#local-passwordresetfailurecount").val();
      var pwp_lockoutdur = $("#local-passwordlockoutduration").val();
      var pwp_minlen = $("#local-passwordminlength").val();
      var pwp_mindigits = $("#local-passwordmindigits").val();
      var pwp_minalphas = $("#local-passwordminalphas").val();
      var pwp_minuppers = $("#local-passwordminuppers").val();
      var pwp_minlowers = $("#local-passwordminlowers").val();
      var pwp_minspecials = $("#local-passwordminspecials").val();
      var pwp_min8bits = $("#local-passwordmin8bit").val();
      var pwp_maxrepeats = $("#local-passwordmaxrepeats").val();
      var pwp_mincat = $("#local-passwordmincategories").val();
      var pwp_mintoken = $("#local-passwordmintokenlength").val();
      var pwp_badwords = $("#local-passwordbadwords").val();
      var pwp_userattrs = $("#local-passworduserattributes").val();
      var pwp_maxseq = $("#local-passwordmaxsequence").val();
      var pwp_maxseqset = $("#local-passwordmaxseqsets").val();
      var pwp_maxclass = $("#local-passwordmaxclasschars").val();
      var pwp_scheme = $("#local-passwordstoragescheme").val();

      var pwp_type = "User Policy";
      if ( $("#subtree-pwp-radio").is(":checked")) {
        pwp_type = "Subtree Policy";
      }

      /*
       * Go through all the settings and create an arg list, but if editing
       * the policy and the value is now "", we need to set it to the default
       * value.
       */
      arg_list = [];

      // Do the on/off settings first
      arg_list.push('--pwdtrack=' + pwp_track);
      arg_list.push('--pwdchange=' + pwp_passwordchange);
      arg_list.push('--pwdmustchange=' + pwp_passwordmustchange);
      arg_list.push('--pwdhistory=' + pwp_history);
      arg_list.push('--pwdexpire=' + pwp_exp);
      arg_list.push('--pwdlockout=' + pwp_lockout);
      arg_list.push('--pwdunlock=' + pwp_unlock);
      arg_list.push('--pwdchecksyntax=' + pwp_checksyntax);
      arg_list.push('--pwdpalindrome=' + pwp_palindrome);
      arg_list.push('--pwddictcheck=' + pwp_dictcheck);
      arg_list.push('--pwdsendexpiring=' + pwp_sendexp);
      // Do the rest
      if ( !add_validate_arg (arg_list, "num", pwp_inhistory, "0", edit, 'passwordinhistory', '--pwdhistorycount', 'Passwords in history') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_minage, "0", edit, 'passwordminage', '--pwdminage', 'Allowed Password Changes') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_maxage, "0", edit, 'passwordmaxage', '--pwdmaxage', 'Password Expiration Time') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_gracelimit, "0", edit, 'passwordgracelimit', '--pwdgracelimit', 'Allowed Logins') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_inhistory, "0", edit, 'passwordwarning', '--pwdwarning', 'Password Warning') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_maxfailure, "0", edit, 'passwordmaxfailure', '--pwdmaxfailures', 'Number of Failed Logins') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_failcount, "0", edit, 'passwordresetfailurecount', '--pwdresetfailcount', 'Failure Count Reset') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_lockoutdur, "0", edit, 'passwordlockoutduration', '--pwdlockoutduration', 'Time Until Account Unlock') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_minlen, "6", edit, 'passwordminlength', '--pwdminlen', 'Password Minimum Length') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_mindigits, "0", edit, 'passwordmindigits', '--pwdmindigits', 'Minimum Digits') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_minalphas, "0", edit, 'passwordminalphas', '--pwdminalphas', 'Minimum Alphas') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_minuppers, "0", edit, 'passwordminuppers', '--pwdminuppers', 'Minimum Uppercase Characters') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_minlowers, "0", edit, 'passwordminlowers', '--pwdminlowers', 'Minimum Lowercase Characters') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_minspecials, "0", edit, 'passwordminspecials', '--pwdminspecials', 'Minimum Special Characters') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_min8bits, "0", edit, 'passwordmin8bits', '--pwdmin8bits', 'Minimum Alphas') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_maxrepeats, "0", edit, 'passwordmaxrepeats', '--pwdmaxrepeats', 'Maximum Repeats') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_mincat, "3", edit, 'passwordmincategories', '--pwdmincatagories', 'Minimum Catagories') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_mintoken, "3", edit, 'passwordmintokenlength', '--pwdmintokenlen', 'Minimum Alphas') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_maxseq, "0", edit, 'passwordmaxsequence', '--pwdmaxseq', 'Maximum Sequence') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_maxseqset, "0", edit, 'passwordmaxseqsets', '--pwdmaxseqsets', 'Maximum Sequence Sets') ) { return; }
      if ( !add_validate_arg (arg_list, "num", pwp_maxclass, "0", edit, 'passwordmaxclasschars', '--pwdmaxclasschars', 'Maximum Character Classes') ) { return; }
      if ( !add_validate_arg (arg_list, "dn", pwp_admin, "", edit, 'passwordadmindn', '--pwdadmin', 'Password Administrator') ) { return; }
      if (pwp_badwords != "" || (edit && localpwp_values['passwordbadwords'] !== undefined && pwp_badwords != localpwp_values['passwordbadwords'])) {
        arg_list.push('--pwdbadwords=' + pwp_badwords);
      }
      if (pwp_userattrs != "" || (edit && localpwp_values['passworduserattributes'] !== undefined && pwp_userattrs != localpwp_values['passworduserattributes'])) {
        arg_list.push('--pwduserattrs=' + pwp_userattrs);
      }
      if (pwp_scheme != "" || (edit && localpwp_values['passwordstoragescheme'] !== undefined && pwp_scheme != localpwp_values['passwordstoragescheme'])) {
        arg_list.push('--pwdscheme=' + pwp_scheme);
      }

      /*
       * Update/Add Password policy to DS
       */
      if ( edit ) {
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','localpwp', 'set', policy_name];
        cmd = cmd.concat(arg_list);
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
          popup_success('Successfully edited local password policy');
          $("#local-pwp-form").modal('toggle')
        }).fail(function(data) {
            popup_err("Failed to edit local password policy", data.message);
        });
      } else {
        // Create new local policy
        var action = "addsubtree";
        if (pwp_type == "User Policy") {
          action = "adduser";
        }
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','localpwp', action, policy_name];
        cmd = cmd.concat(arg_list);
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
          pwp_table.row.add( [
              policy_name,
              pwp_type,
              local_pwp_html
          ] ).draw( false );
          popup_success('Successfully created local password policy');
          $("#local-pwp-form").modal('toggle');
        }).fail(function(data) {
            popup_err("Failed to create local password policy", data.message);
        });
      }
    });

    // Delete local password policy
    $(document).on('click', '.delete-local-pwp', function(e) {
      e.preventDefault();
      // Update HTML table
      var data = pwp_table.row( $(this).parents('tr') ).data();
      var del_pwp_name = data[0];
      var pwp_row = $(this);
      popup_confirm("Are you sure you want to delete local password policy: <b>" + del_pwp_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          // Delete pwp from DS
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','localpwp', 'remove', del_pwp_name];
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            // Update html table
            pwp_table.row( pwp_row.parents('tr') ).remove().draw( false );
            popup_success('Successfully deleted local password policy');
          }).fail(function(data) {
              popup_err("Failed to delete local password policy\n" + data.message);
          });
        }
      });
    });

    // SASL Mappings Form
    $("#sasl-map-save").on("click", function() {
      var sasl_map_name = $("#sasl-map-name").val();
      var sasl_regex =  $("#sasl-map-regex").val();
      var sasl_base =  $("#sasl-map-base").val();
      var sasl_filter = $("#sasl-map-filter").val();
      var sasl_priority = $("#sasl-map-priority").val();

      // Validate values
      if (sasl_map_name == '') {
        report_err($("#sasl-map-name"), 'You must provide a mapping name');
        return;
      }
      if (sasl_map_name == '') {
        report_err($("#sasl-map-regex"), 'You must provide an regex');
        return;
      }
      if (sasl_regex == '') {
        report_err($("#sasl-map-base"), 'You must provide a base DN template');
        return;
      }
      if (sasl_filter == '') {
        report_err($("#sasl-map-filter"), 'You must provide an filter template');
        return;
      }
      if (sasl_priority == '') {
        sasl_priority = '100'
      } else if (valid_num(sasl_priority)) {
        var priority = Number(sasl_priority);
        if (priority < 1 || priority > 100) {
          report_err($("#sasl-map-priority"), 'You must provide a number between 1 and 100');
          return;
        }
      } else {
        report_err($("#sasl-map-priority"), 'You must provide a number between 1 and 100');
        return;
      }

      // Build command line args
      var sasl_name_cmd = "--cn=" + sasl_map_name ;
      var sasl_regex_cmd = "--nsSaslMapRegexString=" + sasl_regex;
      var sasl_base_cmd = "--nsSaslMapBaseDNTemplate=" + sasl_base;
      var sasl_filter_cmd = "--nsSaslMapFilterTemplate=" + sasl_filter;
      var sasl_priority_cmd = "--nsSaslMapPriority=" + sasl_priority;

      if ( $("#sasl-header").html().includes("Create") ) {
        // Create new mapping and update table
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','sasl', 'create',
                   sasl_name_cmd, sasl_regex_cmd, sasl_base_cmd, sasl_filter_cmd, sasl_priority_cmd];
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
          // Update html table
          sasl_table.row.add( [
            sasl_map_name,
            sasl_regex,
            sasl_base,
            sasl_filter,
            sasl_priority,
            sasl_action_html
          ] ).draw( false );
          popup_success("Successfully added new SASL mapping");
          $("#sasl-map-form").modal('toggle');
        }).fail(function(data) {
          popup_err("Failure Adding SASL Mapping",
                    "Failed To Add SASL Mapping: <b>" + sasl_map_name + "</b>: \n" + data.message);
          $("#sasl-map-form").modal("toggle");
        });
      } else {
        // Editing mapping.  First delete the old mapping
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','sasl', 'delete', sasl_map_name];
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
          // Remove row from old
          sasl_table.rows( function ( idx, data, node ) {
            return data[0] == sasl_map_name;
          }).remove().draw();

          // Then add new mapping and update table
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','sasl', 'create',
                     sasl_name_cmd, sasl_regex_cmd, sasl_base_cmd, sasl_filter_cmd, sasl_priority_cmd];
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
            // Update html table
            sasl_table.row.add( [
              sasl_map_name,
              sasl_regex,
              sasl_base,
              sasl_filter,
              sasl_priority,
              sasl_action_html
            ] ).draw( false );
            popup_success("Successfully added new SASL mapping");
            $("#sasl-map-form").modal('toggle');
          }).fail(function(data) {
            popup_err("Failure Adding SASL Mapping",
                      "Failed To Add SASL Mapping: <b>" + sasl_map_name + "</b>: \n" + data.message);
            $("#sasl-map-form").modal("toggle");
          });
        }).fail(function(data) {
          popup_err("Failure Deleting Old SASL Mapping",
                    "Failed To Delete SASL Mapping: <b>" + sasl_map_name + "</b>: \n" + data.message);
          $("#sasl-map-form").modal("toggle");
        });
      }
    });

    /*
     *  Stop, Start, and Restart server
     */
    $("#start-server-btn").on("click", function () {
      $("#ds-start-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Starting instance <b>" + server_id + "</b>...");
      $("#start-instance-form").modal('toggle');
      var cmd = [DSCTL, server_inst, 'start'];
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        $("#start-instance-form").modal('toggle');
        load_config();
        popup_success("Started instance \"" + server_id + "\"");
      }).fail(function(data) {
        $("#start-instance-form").modal('toggle');
        popup_err("Error", "Failed to start instance \"" + server_id + "\"\n" + data.message);
      });
    });

    $("#stop-server-btn").on("click", function () {
      $("#ds-stop-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Stopping instance <b>" + server_id + "</b>...");
      $("#stop-instance-form").modal('toggle');
      var cmd = [DSCTL, server_inst, 'stop'];
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        $("#stop-instance-form").modal('toggle');
        popup_success("Stopped instance \"" + server_id + "\"");
        check_inst_alive();
      }).fail(function(data) {
        $("#stop-instance-form").modal('toggle');
        popup_err("Error", "Failed to stop instance \"" + server_id + "\"\n" + data.message);
        check_inst_alive();
      });
    });

    $("#restart-server-btn").on("click", function () {
      $("#ds-restart-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Retarting instance <b>" + server_id + "</b>...");
      $("#restart-instance-form").modal('toggle');
      var cmd = [DSCTL, server_inst, 'restart'];
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        $("#restart-instance-form").modal('toggle');
        load_config();
        popup_success("Restarted instance \"" + server_id + "\"");
      }).fail(function(data) {
        $("#restart-instance-form").modal('toggle');
        popup_err("Error", "Failed to restart instance \"" + server_id + "\"\n" + data.message);
      });
    });

    /* Backup server */
    $("#ds-backup-btn").on('click', function () {
      var backup_name = $("#backup-name").val();
      if (backup_name == ""){
        popup_msg("Error", "Backup must have a name");
        return;
      }
      if (backup_name.indexOf(' ') >= 0) {
        popup_msg("Error", "Backup name can not contain any spaces");
        return;
      }
      if (backup_name.indexOf('/') === -1) {
        popup_msg("Error", "Backup name can not contain a forward slash. " +
                           "Backups are written to the server's backup directory (nsslapd-bakdir)");
        return;
      }
      var cmd = ['status-dirsrv', server_inst];
      $("#backup-spinner").show();
      cockpit.spawn(cmd, { superuser: true}).
      done(function() {
        var cmd = [DSCONF, server_inst, 'backup', 'create',  backup_name];
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
        done(function(data) {
          $("#backup-spinner").hide();
          popup_success("Backup has been created");
          $("#backup-form").modal('toggle');
        }).
        fail(function(data) {
          $("#backup-spinner").hide();
          popup_err("Error", "Failed to backup the server\n" + data.message);
        })
      }).
      fail(function() {
        var cmd = [DSCTL, server_inst, 'db2bak', backup_name];
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
        done(function(data) {
          $("#backup-spinner").hide();
          popup_success("Backup has been created");
          $("#backup-form").modal('toggle');
        }).
        fail(function(data) {
          $("#backup-spinner").hide();
          popup_err("Error", "Failed to backup the server\n" + data.message);
        });
      });
    });

    $("#backup-server-btn").on('click', function () {
      $("#backup-name").val("");
    });

    /* Restore.  load restore table with current backups */
    $("#restore-server-btn").on('click', function () {
      var cmd = [DSCTL, server_id, '-j', 'backups'];
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        var backup_btn = "<button class=\"btn btn-default restore-btn\" type=\"button\">Restore</button>";
        var del_btn =  "<button title=\"Delete backup directory\" class=\"btn btn-default ds-del-backup-btn\" type=\"button\"><span class='glyphicon glyphicon-trash'></span></button>";
        var obj = JSON.parse(data);
        backup_table.clear().draw( false );
        for (var i = 0; i < obj.items.length; i++) {
          var backup_name = obj.items[i][0];
          var backup_date = obj.items[i][1];
          var backup_size = obj.items[i][2];
          backup_table.row.add([backup_name, backup_date, backup_size, backup_btn, del_btn]).draw( false );
        }
      }).fail(function(data) {
        popup_err("Error", "Failed to get list of backups\n" + data.message);
      });
    });

    /* Restore server */
    $(document).on('click', '.restore-btn', function(e) {
      e.preventDefault();
      var data = backup_table.row( $(this).parents('tr') ).data();
      var restore_name = data[0];
      popup_confirm("Are you sure you want to restore this backup:  <b>" + restore_name + "<b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = ['status-dirsrv', server_inst];
          $("#restore-spinner").show();
          cockpit.spawn(cmd, { superuser: true}).
          done(function() {
            var cmd = [DSCONF, server_inst, 'backup', 'restore',  restore_name];
            cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
            done(function(data) {
              $("#restore-spinner").hide();
              popup_success("The backup has been restored");
              $("#restore-form").modal('toggle');
            }).
            fail(function(data) {
              $("#restore-spinner").hide();
              popup_err("Error", "Failed to restore from the backup\n" + data.message);
            });
          }).
          fail(function() {
            var cmd = [DSCTL, server_inst, 'bak2db', restore_name];
            cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).
            done(function(data) {
              $("#restore-spinner").hide();
              popup_success("The backup has been restored");
              $("#restore-form").modal('toggle');
            }).
            fail(function(data) {
              $("#restore-spinner").hide();
              popup_err("Error", "Failed to restore from the backup\n" + data.message);
            });
          });
        }
      });
    });

    /* Delete backup directory */
    $(document).on('click', '.ds-del-backup-btn', function(e) {
      e.preventDefault();
      var data = backup_table.row( $(this).parents('tr') ).data();
      var restore_name = data[0];
      var backup_row = $(this);
      popup_confirm("Are you sure you want to delete this backup: <b>" + restore_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCTL, server_inst, 'backups', '--delete', restore_name];
          $("#restore-spinner").show();
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            $("#restore-spinner").hide();
            backup_table.row( backup_row.parents('tr') ).remove().draw( false );
            popup_success("The backup has been deleted");
          }).fail(function(data) {
            $("#restore-spinner").hide();
            popup_err("Error", "Failed to delete the backup\n" + data.message);
          });
        }
      });
    });

    /* reload schema */
    $("#schema-reload-btn").on("click", function () {
      var schema_dir = $("#reload-dir").val();
      if (schema_dir != ""){
        var cmd = [DSCONF, server_id, 'schema', 'reload', '--schemadir', schema_dir, '--wait'];
      } else {
        var cmd = [DSCONF, server_id, 'schema', 'reload', '--wait'];
      }
      $("#reload-spinner").show();
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        popup_msg("Success", "Successfully reloaded schema");  // TODO use timed interval success msg (waiting for another PR top be merged before we can add it)
        $("#schema-reload-form").modal('toggle');
        $("#reload-spinner").hide();
      }).fail(function(data) {
        popup_err("Error", "Failed to reload schema files\n" + data.message);
        $("#reload-spinner").hide();
      });
    });


    // Remove instance
    $("#remove-server-btn").on("click", function () {
      popup_confirm("Are you sure you want to this remove instance: <b>" + server_id + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCTL, server_id, "remove", "--doit"];
          $("#ds-remove-inst").html("<span class=\"spinner spinner-xs spinner-inline\"></span> Removing instance <b>" + server_id + "</b>...");
          $("#remove-instance-form").modal('toggle');
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            $("#remove-instance-form").modal('toggle');
            popup_msg("Success", "Instance has been deleted");
            get_insts();
          }).fail(function(data) {
            $("#remove-instance-form").modal('toggle');
            popup_err("Error", "Failed to remove instance\n" + data.message);
          });
        }
      });
    });

    // Create instance form
    $("#create-server-btn").on("click", function() {
      clear_inst_form();
      set_ports();
    });
    $("#no-inst-create-btn").on("click", function () {
      clear_inst_form();
    });

    // Create Instance
    $("#create-inst-save").on("click", function() {
      $(".ds-modal-error").hide();
      $(".ds-inst-input").css("border-color", "initial");

      /*
       * Validate settings and update the INF settings
       */
      var setup_inf = create_inf_template;

      // Server ID
      var new_server_id = $("#create-inst-serverid").val();
      if (new_server_id == ""){
        report_err($("#create-inst-serverid"), 'You must provide an Instance name');
        return;
      } else {
        new_server_id = new_server_id.replace(/^slapd-/i, "");  // strip "slapd-"
        setup_inf = setup_inf.replace('INST_NAME', new_server_id);
      }

      // Port
      var server_port = $("#create-inst-port").val();
      if (server_port == ""){
        report_err($("#create-inst-port"), 'You must provide a port number');
        return;
      } else if (!valid_num(server_port)) {
        report_err($("#create-inst-port"), 'Port must be a number!');
        return;
      } else {
        setup_inf = setup_inf.replace('PORT', server_port);
      }

      // Secure Port
      var secure_port = $("#create-inst-secureport").val();
      if (secure_port == ""){
        report_err($("#create-inst-secureport"), 'You must provide a secure port number');
        return;
      } else if (!valid_num(secure_port)) {
        report_err($("#create-inst-secureport"), 'Secure port must be a number!');
        return;
      } else {
        setup_inf = setup_inf.replace('SECURE_PORT', secure_port);
      }

      // Root DN
      var server_rootdn = $("#create-inst-rootdn").val();
      if (server_rootdn == ""){
        report_err($("#create-inst-rootdn"), 'You must provide a Directory Manager DN');
        return;
      } else {
        setup_inf = setup_inf.replace('ROOTDN', server_rootdn);
      }

      // Setup Self-Signed Certs
      if ( $("#create-inst-tls").is(":checked") ){
        setup_inf = setup_inf.replace('SELF_SIGN', 'True');
      } else {
        setup_inf = setup_inf.replace('SELF_SIGN', 'False');
      }

      // Root DN password
      var root_pw = $("#rootdn-pw").val();
      var root_pw_confirm = $("#rootdn-pw-confirm").val();
      if (root_pw != root_pw_confirm) {
        report_err($("#rootdn-pw"), 'Directory Manager passwords do not match!');
        $("#rootdn-pw-confirm").css("border-color", "red");
        return;
      } else if (root_pw == ""){
        report_err($("#rootdn-pw"), 'Directory Manager password can not be empty!');
        $("#rootdn-pw-confirm").css("border-color", "red");
        return;
      } else {
        setup_inf = setup_inf.replace('ROOTPW', root_pw);
      }

      // Backend/Suffix
      var backend_name = $("#backend-name").val();
      var backend_suffix = $("#backend-suffix").val();
      if ( (backend_name != "" && backend_suffix == "") || (backend_name == "" && backend_suffix != "") ) {
        if (backend_name == ""){
          report_err($("#backend-name"), 'If you specify a backend suffix, you must also specify a backend name');
          return;
        } else {
          report_err($("#backend-suffix"), 'If you specify a backend name, you must also specify a backend suffix');
          return;
        }
      }
      if (backend_name != ""){
        // We definitely have a backend name and suffix, next validate the suffix is a DN
        if (valid_dn(backend_suffix)) {
          // It's valid, add it
          setup_inf += "\n[backend-" + backend_name + "]\nsuffix = " + backend_suffix + "\n";
        } else {
          // Not a valid DN
          report_err($("#backend-suffix"), 'Invalid DN for Backend Suffix');
          return;
        }
        if ( $("#create-sample-entries").is(":checked") ) {
          setup_inf += '\nsample_entries = yes\n';
        } else {
          setup_inf += '\nsample_entries = no\n';
        }
      }

      /*
       * Here are steps we take to create the instance
       *
       * [1] Get FQDN Name for nsslapd-localhost setting in setup file
       * [2] Create a file for the inf setup parameters
       * [3] Set strict permissions on that file
       * [4] Populate the new setup file with settings (including cleartext password)
       * [5] Create the instance
       * [6] Remove setup file
       */
      cockpit.spawn(["hostname", "--fqdn"], { superuser: true, "err": "message" }).fail(function(ex) {
        // Failed to get FQDN
        popup_err("Failed to get hostname!", ex.message);
      }).done(function (data){
        /*
         * We have FQDN, so set the hostname in inf file, and create the setup file
         */
        setup_inf = setup_inf.replace('FQDN', data);
        var setup_file = "/tmp/389-setup-" + (new Date).getTime() + ".inf";
        var rm_cmd = ['rm', setup_file];
        var create_file_cmd = ['touch', setup_file];
        cockpit.spawn(create_file_cmd, { superuser: true, "err": "message" }).fail(function(ex) {
          // Failed to create setup file
          popup_err("Failed to create installation file!", ex.message);
        }).done(function (){
          /*
           * We have our new setup file, now set permissions on that setup file before we add sensitive data
           */
          var chmod_cmd = ['chmod', '600', setup_file];
          cockpit.spawn(chmod_cmd, { superuser: true, "err": "message" }).fail(function(ex) {
            // Failed to set permissions on setup file
            cockpit.spawn(rm_cmd, { superuser: true });  // Remove Inf file with clear text password
            $("#create-inst-spinner").hide();
            popup_err("Failed to set permission on setup file " + setup_file + ": ", ex.message);
          }).done(function (){
            /*
             * Success we have our setup file and it has the correct permissions.
             * Now populate the setup file...
             */
            var cmd = ["/bin/sh", "-c", '/usr/bin/echo -e "' + setup_inf + '" >> ' + setup_file];
            cockpit.spawn(cmd, { superuser: true, "err": "message" }).fail(function(ex) {
              // Failed to populate setup file
              popup_err("Failed to populate installation file!", ex.message);
            }).done(function (){
              /*
               * Next, create the instance...
               */
              cmd = [DSCREATE, 'fromfile', setup_file];
              cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV] }).fail(function(ex) {
                // Failed to create the new instance!
                cockpit.spawn(rm_cmd, { superuser: true });  // Remove Inf file with clear text password
                $("#create-inst-spinner").hide();
                popup_err("Failed to create instance!", ex.message);
              }).done(function (){
                // Success!!!  Now cleanup everything up...
                cockpit.spawn(rm_cmd, { superuser: true });  // Remove Inf file with clear text password
                $("#create-inst-spinner").hide();
                $("#server-list-menu").attr('disabled', false);
                $("#no-instances").hide();
                get_insts();  // Refresh server list
                popup_msg("Success!", "Successfully created instance:  <b>slapd-" + new_server_id + "</b>", );
                $("#create-inst-form").modal('toggle');
              });
            });
            $("#create-inst-spinner").show();
          });
        });
      });
    });

    // Accordion opening/closings
    $(".ds-accordion-panel").css('display','none');

    $("#config-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Advanced Settings ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Advanced Settings ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    $("#tuning-config-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Advanced Settings ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Advanced Settings ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    $('#local-pwp-suffix').on('change', function () {
      // Reload the table for the new selected suffix
      get_and_set_localpwp();
    });

    // Edit Local Password Policy
    $(document).on('click', '.edit-local-pwp', function(e) {
      e.preventDefault();
      clear_local_pwp_form();

      var data = pwp_table.row( $(this).parents('tr') ).data();
      var policy_name = data[0];

      // lookup the entry, and get the current settings
      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','localpwp', 'get', policy_name];
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        localpwp_values = {};  // Clear it out
        var obj = JSON.parse(data);
        for (var attr in obj['attrs']) {
          var val = obj['attrs'][attr];
          $("#local-" + attr).val(val);
          if (val == "on") {
            $("#local-" + attr).prop('checked', true);
            $("#local" + attr).trigger('change');
          } else if (val == "off") {
            $("#local" + attr).prop('checked', false);
            $("#local" + attr).trigger('change');
          }
          localpwp_values[attr] = val;
        }
        if ( obj['pwp_type'] == "User") {
          $("#user-pwp-radio").prop("checked", true );
        } else {
          $("#subtree-pwp-radio").prop("checked", true );
        }

        // Set the form header and fields
        $("#local-pwp-header").html("<b>Edit Local Password Policy</b>");
        $("#local-entry-dn").val(policy_name);
        // Disable radio buttons
        $("#subtree-pwp-radio").attr('disabled', true);
        $("#user-pwp-radio").attr('disabled', true);
        $("#local-entry-dn").attr('disabled', true);

        // Open form
        $("#local-pwp-form").modal('toggle');

      }).fail(function(data) {
          popup_err("Failed to get local password policy", data.message);
      });
    });

    // Mark this page as loaded
    server_page_loaded = 1;
  }); // servers.html loaded
}); // Document ready
