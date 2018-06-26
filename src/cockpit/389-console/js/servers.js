
var sasl_action_html =
  '<div class="dropdown">' +
    '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="dropdownMenu1" data-toggle="dropdown">' +
      'Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="dropdownMenu1">' +
      '<li role=""><a role="menuitem" class="sasl-edit-btn" tabindex="0" href="#">Edit Mapping</a></li>' +
      '<li role=""><a role="menuitem" class="sasl-verify-btn" tabindex="0" href="#">Test Mapping</a></li>' +
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
  "user = USER\n" +
  "group = GROUP\n" +
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
  "user = USER\n" +
  "group = GROUP\n" +
  "instance_name = INST_NAME\n" +
  "port = PORT\n" +
  "root_dn = ROOTDN\n" +
  "root_password = ROOTPW\n" +
  "secure_port = SECURE_PORT\n" +
  "self_sign_cert = SELF_SIGN\n";


function load_server_config() {
  var mark = document.getElementById("server-config-title");
  mark.innerHTML = "Configuration for server: <b>" + server_id + "</b>";
}

function server_hide_all(){
  // Jquery can problem do this better using names/roles
  $("#server-config").hide();
  $("#server-security").hide();
  $("#server-logs").hide();
  $("#server-pwpolicy").hide();
  $("#server-tuning").hide();
  $("#server-tuning").hide();
};

function clear_sasl_map_form () {
  $("#sasl-map-name").val("");
  $("#sasl-map-regex").val("");
  $("#sasl-map-base").val("");
  $("#sasl-map-filter").val("");
  $("#sasl-map-priority").val("");
}

function clear_local_pwp_form () {
  $("#local-entry-dn").val("");
  $("#local-passwordtrackupdatetime").prop('checked', false);
  $("#local-passwordadmindn").val("");
  $("#local-passwordchange").prop('checked', false);
  $("#local-passwordmustchange").prop('checked', false);
  $("#local-passwordhistory").prop('checked', false);
  $("#local-passwordinhistory").val("");
  $("#local-passwordminage").val("");
  $("#local-passwordexp").prop('checked', false);
  $("#local-passwordmaxage").val("");
  $("#local-passwordgracelimit").val("");
  $("#local-passwordwarning").val("");
  $("#local-passwordsendexpiringtime").prop('checked', false);
  $("#local-passwordlockout").prop('checked', false);
  $("#local-passwordmaxfailure").val("");
  $("#local-passwordresetfailurecount").val("");
  $("#local-passwordunlock").prop('checked', false);
  $("#local-passwordlockoutduration").val("");
  $("#local-passwordchecksyntax").prop('checked', false);
  $("#local-passwordminlength").val("");
  $("#local-passwordmindigits").val("");
  $("#local-passwordminalphas").val("");
  $("#local-passwordminuppers").val("");
  $("#local-passwordminlowers").val("");
  $("#local-passwordminspecials").val("");
  $("#local-passwordmin8bit").val("");
  $("#local-passwordmaxrepeats").val("");
  $("#local-passwordmincategories").val("");
  $("#local-passwordmintokenlength").val("");
  $("#local-passwordstoragescheme").prop('selectedIndex',0);
  $("#subtree-pwp-radio").prop('checked', true);
  $("#subtree-pwp-radio").attr('disabled', false);
  $("#user-pwp-radio").attr('disabled', false);
}

function clear_inst_input() {
  // Reset the color of the fields
  $("#create-inst-serverid").css("border-color", "initial");
  $("#create-inst-port").css("border-color", "initial");
  $("#create-inst-secureport").css("border-color", "initial");
  $("#create-inst-rootdn").css("border-color", "initial");
  $("#create-inst-user").css("border-color", "initial");
  $("#create-inst-group").css("border-color", "initial");
  $("#rootdn-pw").css("border-color", "initial");
  $("#rootdn-pw-confirm").css("border-color", "initial");
  $("#backend-name").css("border-color", "initial");
  $("#backend-suffix").css("border-color", "initial");
}

function clear_inst_form() {
  $(".ds-modal-error").hide();
  $("#create-inst-serverid").val("");
  $("#create-inst-port").val("389");
  $("#create-inst-secureport").val("636");
  $("#create-inst-rootdn").val("cn=Directory Manager");
  $("#create-inst-user").val("dirsrv");
  $("#create-inst-group").val("dirsrv");
  $("#rootdn-pw").val("");
  $("#rootdn-pw-confirm").val("");
  $("#backend-suffix").val("");
  $("#backend-name").val("");
  $("#create-inst-tls").prop('checked', true);
  clear_inst_input();
}


function get_and_set_config () {
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','config', 'get'];
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var obj = JSON.parse(data);
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
    }
    check_inst_alive();
  }).fail(function(data) {
      console.log("failed: " + data.message);
      check_inst_alive();
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

  // Build dsconf commands to apply all the mods
  if (mod_list.length) {
    apply_mods(mod_list);
  } else {
    // No changes to save, log msg?  popup_msg()
  }
}

// load the server config pages
$(document).ready( function() {
  $("#main-banner").load("banner.html");

  // Fill in the server instance dropdown
  get_insts();
  check_for_389();

  $("#server-tab").css( 'color', '#228bc0');

  $("#server-content").load("servers.html", function () {
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

    var sasl_table = $('#sasl-table').DataTable( {
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

    $("#create-sasl-map-btn").on("click", function () {
      clear_sasl_map_form();
    });

    // Edit SASL mapping
    $(document).on('click', '.sasl-edit-btn', function(e) {
        // TODO - get this working
        e.preventDefault();
        clear_sasl_map_form();
        var data = sasl_table.row( $(this).parents('tr') ).data();
        var edit_sasl_name = data[0];

        $("#sasl-header").html("Edit SASL Mapping");
        $("#sasl-map-name").val(edit_sasl_name);
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
          // TODO Delete mapping from DS

          // Update html table
          sasl_table.row( sasl_row.parents('tr') ).remove().draw( false );
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
    var pwp_table = $('#passwd-policy-table').DataTable( {
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

    // Save the global password Policy
    $("#server-pwp-save-btn").on("click"), function() {
      // Get the values
      var pwp_track = "off";
      if ( $("#passwordtrackupdatetime").is(":checked") ) {
        pwp_track = "on";
      }
      var pwp_admin = $("#passwordadmindn").val();
      var pwp_passwordchange = "off";
      if ($("#passwordchange").is(":checked") ){
        pwp_passwordchange = "on";
      }
      var pwp_passwordmustchange = "off";
      if ($("#passwordmustchange").is(":checked") ){
        pwp_passwordmustchange = "on";
      }
      var pwp_history = "off";
      if ($("#passwordhistory").is(":checked") ){
        pwp_history = "on";
      }
      var pwp_inhistory = $("#passwordinhistory").val();
      var pwp_minage = $("#passwordminage").val();
      var pwp_exp = "off";
      if ($("#passwordexp").is(":checked") ){
        pwp_exp = "on";
      }
      var pwp_maxage = $("#passwordmaxage").val();
      var pwp_gracelimit = $("#passwordgracelimit").val();
      var pwp_warning = $("#passwordwarning").val();

      var pwp_sendexp = "off";
      if ($("#passwordsendexpiringtime").is(":checked") ){
        pwp_sendexp = "on";
      }
      var pwp_lockout = "off";
      if ($("#passwordlockout").is(":checked") ){
        pwp_lockout = "on";
      }
      var pwp_maxfailure = $("#passwordmaxfailure").val();
      var pwp_failcount = $("#passwordresetfailurecount").val();
      var pwp_unlock = "off";
      if ($("#passwordunlock").is(":checked") ){
        pwp_unlock = "on";
      }
      var pwp_lockoutdur = $("#passwordlockoutduration").val();
      var pwp_checksyntax = "off";
      if ($("#passwordchecksyntax").is(":checked") ){
        pwp_checksyntax = "on";
      }
      var pwp_minlen = $("#passwordminlength").val();
      var pwp_mindigit = $("#passwordmindigits").val();
      var pwp_minalphas = $("#passwordminalphas").val();
      var pwp_minuppers = $("#passwordminuppers").val();
      var pwp_minlowers = $("#passwordminlowers").val();
      var pwp_minspecials = $("#passwordminspecials").val();
      var pwp_min8bit = $("#passwordmin8bit").val();
      var pwp_maxrepeats = $("#passwordmaxrepeats").val();
      var pwp_mincat = $("#passwordmincategories").val();
      var pwp_mintoken = $("#passwordmintokenlength").val();

      // TODO  - save to DS

    }
    /*
     *  Modal Forms
     */

    // Local password policy
    $("#create-local-pwp-btn").on("click", function () {
      clear_local_pwp_form();
    });

    $("#local-pwp-save").on("click", function() {
      var policy_name = $("#local-entry-dn").val();
      var pwp_track = "off";
      if ( $("#local-passwordtrackupdatetime").is(":checked") ) {
        pwp_track = "on";
      }
      var pwp_admin = $("#local-passwordadmindn").val();
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
      var pwp_inhistory = $("#local-passwordinhistory").val();
      var pwp_minage = $("#local-passwordminage").val();
      var pwp_exp = "off";
      if ($("#local-passwordexp").is(":checked") ){
        pwp_exp = "on";
      }
      var pwp_maxage = $("#local-passwordmaxage").val();
      var pwp_gracelimit = $("#local-passwordgracelimit").val();
      var pwp_warning = $("#local-passwordwarning").val();

      var pwp_sendexp = "off";
      if ($("#local-passwordsendexpiringtime").is(":checked") ){
        pwp_sendexp = "on";
      }
      var pwp_lockout = "off";
      if ($("#local-passwordlockout").is(":checked") ){
        pwp_lockout = "on";
      }
      var pwp_maxfailure = $("#local-passwordmaxfailure").val();
      var pwp_failcount = $("#local-passwordresetfailurecount").val();
      var pwp_unlock = "off";
      if ($("#local-passwordunlock").is(":checked") ){
        pwp_unlock = "on";
      }
      var pwp_lockoutdur = $("#local-passwordlockoutduration").val();
      var pwp_checksyntax = "off";
      if ($("#local-passwordchecksyntax").is(":checked") ){
        pwp_checksyntax = "on";
      }
      var pwp_minlen = $("#local-passwordminlength").val();
      var pwp_mindigit = $("#local-passwordmindigits").val();
      var pwp_minalphas = $("#local-passwordminalphas").val();
      var pwp_minuppers = $("#local-passwordminuppers").val();
      var pwp_minlowers = $("#local-passwordminlowers").val();
      var pwp_minspecials = $("#local-passwordminspecials").val();
      var pwp_min8bit = $("#local-passwordmin8bit").val();
      var pwp_maxrepeats = $("#local-passwordmaxrepeats").val();
      var pwp_mincat = $("#local-passwordmincategories").val();
      var pwp_mintoken = $("#local-passwordmintokenlength").val();


      var pwp_type = "User Password Policy";
      if ( $("#subtree-pwp-radio").is(":checked")) {
        pwp_type = "Subtree Password Policy";
      }

      // TODO - add to DS

      // Update Pwp Table
      pwp_table.row.add( [
            policy_name,
            pwp_type,
            local_pwp_html
        ] ).draw( false );

      // Done, close the form
      $("#local-pwp-form").modal('toggle');
      clear_local_pwp_form();
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
          // TODO Delete pwp from DS

          // Update html table
          pwp_table.row( pwp_row.parents('tr') ).remove().draw( false );
        }
      });
    });

    // SASL Mappings Form
    $("#sasl-map-save").on("click", function() {
      var sasl_name = $("#sasl-map-name").val();
      var sasl_regex = $("#sasl-map-regex").val();
      var sasl_base = $("#sasl-map-base").val();
      var sasl_filter = $("#sasl-map-filter").val();
      var sasl_priority = $("#sasl-map-priority").val();

      // TODO - Add mapping to DS

      // Update html table
      sasl_table.row.add( [
        sasl_name,
        sasl_regex,
        sasl_base,
        sasl_filter,
        sasl_priority,
        sasl_action_html
      ] ).draw( false );

      // Done
      //$("#sasl-map-form").css('display', 'none');
      $("#sasl-map-form").modal('toggle');
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
      if (backup_name.startsWith('/')) {
        popup_msg("Error", "Backup name can not start with a forward slash.  Backups are written to the server's backup directory (nsslapd-bakdir)");
        return;
      }
      var cmd = [DSCTL, server_inst, 'db2bak', backup_name];
      $("#backup-spinner").show();
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        $("#backup-spinner").hide();
        popup_success("Backup has been created");
        $("#backup-form").modal('toggle');
      }).fail(function(data) {
        $("#backup-spinner").hide();
        popup_err("Error", "Failed to backup the server\n" + data.message);
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
          var cmd = [DSCTL, server_inst, 'bak2db', restore_name];
          $("#restore-spinner").show();
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success("The backup has been restored");
            $("#restore-spinner").hide();
            $("#restore-form").modal('toggle');
          }).fail(function(data) {
            $("#restore-spinner").hide();
            popup_err("Error", "Failed to restore from the backup\n" + data.message);
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
            check_for_389();
            load_config();
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
      clear_inst_input();

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

      // DS User
      var server_user = $("#create-inst-user").val();
      if (server_user == ""){
        report_err($("#create-inst-user"), 'You must provide the server user name');
        return;
      } else {
        setup_inf = setup_inf.replace('USER', server_user);
      }

      // DS Group
      var server_group = $("#create-inst-group").val();
      if (server_group == ""){
        report_err($("#create-inst-group"), 'You must provide the server group name');
        return;
      } else {
        setup_inf = setup_inf.replace('GROUP', server_group);
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
              cmd = [DSCREATE, 'install', setup_file];
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
    // Edit Local Password Policy
    $(document).on('click', '.edit-local-pwp', function(e) {
      e.preventDefault();
      clear_local_pwp_form();

      // Update HTML table
      var data = pwp_table.row( $(this).parents('tr') ).data();
      var policy_name = data[0];

      // TODO - lookup the entry, and get the current settings

      // Set the form header and fields
      $("#local-pwp-form-header").html("<b>Edit Local Password Policy</b>");
      $("#local-entry-dn").val(policy_name);
      // Set radio button for type of policy - TODO

      // Disable radio buttons
      $("#subtree-pwp-radio").attr('disabled', true);
      $("#user-pwp-radio").attr('disabled', true);


      // Open form
      $("#local-pwp-form").modal('toggle');
    });

  });
});
