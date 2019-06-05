var repl_suffix = "";
var prev_repl_role = "no-repl";
var prev_role_button;
var binddn_list_color = "";
var repl_mgr_table;
var current_role = "";
var current_rid = "";
var repl_agmt_table;
var repl_winsync_agmt_table;
var repl_clean_table;
var repl_agmt_values = {};
var repl_winsync_agmt_values = {};
var frac_prefix = "(objectclass=*) $ EXCLUDE";
var agmt_init_intervals = [];
var agmt_init_counter = 0;
var winsync_init_intervals = [];
var winsync_init_counter = 0;

// HTML items
var progress_html = '<p><span class="spinner spinner-xs spinner-inline"></span> Initializing Agreement...</p>';

var agmt_action_html =
  '<div class="dropdown">' +
     '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu">' +
      '<li role=""><a class="repl-agmt-btn agmt-edit-btn" href="#">Edit Agreement</a></li>' +
      '<li role=""><a class="repl-agmt-btn agmt-init-btn" href="#">Initialize Agreement</a></li>' +
      '<li role=""><a class="repl-agmt-btn agmt-send-updates-btn" href="#">Send Updates Now</a></li>' +
      '<li role=""><a class="repl-agmt-btn agmt-enable-btn" href="#">Enable/Disable Agreement</a></li>' +
      '<li role=""><a class="repl-agmt-btn agmt-del-btn" href="#">Delete Agreement</a></li>' +
    '</ul>' +
  '</div>';

var winsync_agmt_action_html =
  '<div class="dropdown">' +
     '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="dropdownMenu2">' +
      '<li role=""><a class="repl-agmt-btn winsync-agmt-edit-btn" href="#">Edit Agreement</a></li>' +
      '<li role=""><a class="repl-agmt-btn winsync-agmt-send-updates-btn" href="#">Send/Receives Updates Now</a></li>' +
      '<li role=""><a class="repl-agmt-btn winsync-agmt-init-btn" href="#">Full Re-synchronization</a></li>' +
      '<li role=""><a class="repl-agmt-btn winsync-agmt-enable-btn" href="#">Enable/Disable Agreement</a></li>' +
      '<li role=""><a class="repl-agmt-btn winsync-agmt-del-btn" href="#">Delete Agreement</a></li>' +
    '</ul>' +
  '</div>';

var cleanallruv_action_html =
  '<button class="btn btn-default ds-agmt-dropdown-button abort_cleanallruv_btn" type="button" class="abort-cleanallruv">Abort Task</button>';

// Attribute to CLI argument mappings
var repl_attr_map = {
  'nsds5replicaid': '--replica-id',
  'nsds5replicapurgedelay': '--repl-purge-delay',
  'nsds5replicatombstonepurgeinterval': '--repl-tombstone-purge-interval',
  'nsds5replicaprecisetombstonepurging': '--repl-fast-tombstone-purging',
  'nsds5replicabinddngroup': '--repl-bind-group',
  'nsds5replicabinddngroupcheckinterval':  '--repl-bind-group-interval',
  'nsds5replicaprotocoltimeout': '--repl-protocol-timeout',
  'nsds5replicabackoffmin': '--repl-backoff-min',
  'nsds5replicabackoffmax': '--repl-backoff-max',
  'nsds5replicareleasetimeout': '--repl-release-timeout',
  'nsds5flags': '',
  'nsds5replicatype': '',
  'nsds5replicabinddn': '',
  'nsslapd-changelogdir': '--cl-dir',
  'nsslapd-changelogmaxentries': '--max-entries',
  'nsslapd-changelogmaxage': '--max-age',
  'nsslapd-changelogcompactdb-interval': '--compact-interval',
  'nsslapd-changelogtrim-interval': '--trim-interval'
};

var repl_cl_attrs = ['nsslapd-changelogdir', 'nsslapd-changelogmaxentries', 'nsslapd-changelogmaxage',
                     'nsslapd-changelogcompactdb-interval', 'nsslapd-changelogtrim-interval'];

var repl_attrs = ['nsds5replicaid', 'nsds5replicapurgedelay', 'nsds5replicatombstonepurgeinterval',
                  'nsds5replicaprecisetombstonepurging', 'nsds5replicabinddngroup',
                  'nsds5replicabinddngroupcheckinterval', 'nsds5replicaprotocoltimeout', 'nsds5replicabackoffmin',
                  'nsds5replicabackoffmax', 'nsds5replicareleasetimeout'];


// Helper functions
function clear_agmt_wizard () {
  // Clear input fields and reset dropboxes
  $('.ds-agmt-schedule-checkbox').prop('checked', false);
  $('#agmt-schedule-checkbox').prop('checked', true);
  $("#agmt-start-time").val("00:00");
  $("#agmt-end-time").val("00:15");
  $(".ds-agmt-wiz-dropdown").prop('selectedIndex',0);
  $(".ds-agmt-panel").css('display','none');
  $(".agmt-form-input").css("border-color", "initial");
  $(".agmt-form-input").val("");
  $('#frac-exclude-list').find('option').remove();
  $('#frac-exclude-tot-list').find('option').remove();
  $('#frac-strip-list').find('option').remove();
  $("#select-attr-list").prop('selectedIndex',-1);
  $("#init-options").prop("selectedIndex", 0);
  $("#init-agmt-dropdown").show();
  $("#agmt-wizard-title").html("<b>Create Replication Agreement</b>");
};

function clear_enable_repl_form () {
  $("#nsds5replicaid-form").css("border-color", "initial");
  $("#nsds5replicaid-form").val("");
  $("#select-enable-repl-role").prop("selectedIndex", 0);
  $("#enable-repl-pw").val("");
  $("#enable-repl-pw-confirm").val("");
  $("#enable-repl-mgr-dn").val("");
  $("#enable-repl-mgr-checkbox").prop('checked', false);
  $("#enable-repl-mgr-passwd").hide();
}

function clear_winsync_agmt_wizard() {
  // Clear out winsync agreement form
  $("#winsync-agmt-cn").val("");
  $("#winsync-nsds7windowsdomain").val("");
  $("#winsync-nsds5replicahost").val("");
  $("#winsync-nsds5replicaport").val("");
  $("#winsync-nsds7windowsreplicasubtree").val("");
  $("#winsync-nsds7directoryreplicasubtree").val("");
  $("#winsync-nsds7newwinusersyncenabled-checkbox").prop('checked', false);
  $("#winsync-nsds7newwingroupsyncenabled-checkbox").prop('checked', false);
  $("#winsync-init-checkbox").prop('checked', false);
  $("#winsync-init-chbx").show();
  $("#winsync-nsds5replicabinddn").val("");
  $("#winsync-nsds5replicacredentials").val("");
  $("#winsync-nsds5replicacredentials-confirm").val("");
  $("#winsync-nsds5replicatransportinfo").prop('selectedIndex', 0);
  $("#winsync-agmt-wizard-title").html("<b>Create Winsync Agreement</b>");
}

function clear_cleanallruv_form () {
  // Clear input fields and reset dropboxes
  $('#force-clean').prop('checked', true);
  $("#cleanallruv-rid").val("");
};

function clear_repl_mgr_form () {
  $("#add-repl-pw").val("");
  $("#add-repl-pw-confirm").val("");
  $("#add-repl-mgr-dn").val("cn=replication manager,cn=config");
  $("#add-repl-mgr-checkbox").prop('checked', false);
  $("#add-repl-mgr-passwd").hide();
}


function add_repl_mgr(dn){
  // First check if manager is set to none
	$("#repl-mgr-table tbody").append(
		"<tr>"+
		"<td class='ds-td'>" + dn +"</td>"+
    "<td class='ds-center'>"+
    "<button type='button' class='btn btn-default remove-repl-mgr' title='Remove the manager from the replication configuration'>" +
    "<span class='glyphicon glyphicon-trash'></span> Remove </button></td>" +
		"</tr>");
};


function do_agmt_init(suffix, agmt_cn, idx) {
  var status_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'init-status', '--suffix=' + suffix, '"' + agmt_cn + '"' ];
  log_cmd('do_agmt_init', 'Get initialization status for agmt', status_cmd);
  cockpit.spawn(status_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var init_stat = JSON.parse(data);
    if (init_stat == 'Agreement successfully initialized.' ||
        init_stat == 'Agreement initialization failed.')
    {
      // Init is done (good or bad)
      get_and_set_repl_agmts();
      clearInterval(agmt_init_intervals[idx]);
    }
  });
}

function do_winsync_agmt_init(suffix, agmt_cn, idx) {
  var status_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'init-status', '--suffix=' + suffix, '"' + agmt_cn + '"' ];
  log_cmd('do_winsync_agmt_init', 'Get initialization status for agmt', status_cmd);
  cockpit.spawn(status_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var init_stat = JSON.parse(data);
    if (init_stat == 'Agreement successfully initialized.' ||
        init_stat == 'Agreement initialization failed.')
    {
      // Init is done (good or bad)
      get_and_set_repl_winsync_agmts();
      clearInterval(agmt_init_intervals[idx]);
    }
  });
}

function get_and_set_repl_winsync_agmts() {
  /*
   * Get the replication agreements for the selected suffix
   */
  var suffix = $("#select-repl-winsync-suffix").val();
  repl_winsync_agmt_table.clear();

  if (suffix) {
    console.log("Loading winsync replication agreements...");
    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'list', '--suffix=' + suffix ];
    log_cmd('get_and_set_repl_winsync_agmts', 'Get the winsync agmts', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      var obj = JSON.parse(data);
      for (var idx in obj['items']) {
        var state = "Enabled";
        var con_host = "";
        var con_port = "";
        var ds_subtree = "";
        var win_subtree = "";
        agmt_attrs = obj['items'][idx]['attrs'];
        var agmt_name = agmt_attrs['cn'][0];

        // Compute state (enabled by default)
        if ('nsds5replicaenabled' in agmt_attrs) {
          if (agmt_attrs['nsds5replicaenabled'][0].toLowerCase() == 'off'){
            state = "Disabled";
          }
        }
        var ws_agmt_init_status = "Initialized";
        if ('nsds5replicalastinitstatus' in agmt_attrs &&
            agmt_attrs['nsds5replicalastinitstatus'][0] != "")
        {
          ws_agmt_init_status = agmt_attrs['nsds5replicalastinitstatus'][0];
          if (ws_agmt_init_status == "Error (0) Total update in progress" ||
              ws_agmt_init_status == "Error (0)")
          {
            ws_agmt_init_status = progress_html;
            var ws_interval_agmt_name = agmt_name;
            var ws_init_status_interval = setInterval(function() {
              var status_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'init-status', '--suffix=' + suffix, '"' + ws_interval_agmt_name + '"' ];
              log_cmd('get_and_set_repl_winsync_agmts', 'Get initialization status for winsync agmt', status_cmd);
              cockpit.spawn(status_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                var init_stat = JSON.parse(data);
                if (init_stat == 'Agreement successfully initialized.' ||
                    init_stat == 'Agreement initialization failed.')
                {
                  // Init is done (good or bad)
                  get_and_set_repl_winsync_agmts();
                  clearInterval(ws_init_status_interval);
                }
              });
            }, 2000);
          } else if (ws_agmt_init_status == "Error (0) Total update succeeded") {
            ws_agmt_init_status = "Initialized";
          }
        } else if (agmt_attrs['nsds5replicalastinitstart'][0] == "19700101000000Z"){
          ws_agmt_init_status = "Not initialized";
        }

        repl_winsync_agmt_table.row.add( [
          agmt_attrs['cn'][0],
          agmt_attrs['nsds5replicahost'][0],
          agmt_attrs['nsds5replicaport'][0],
          state,
          agmt_attrs['nsds5replicalastupdatestatus'][0],
          ws_agmt_init_status,
          winsync_agmt_action_html
        ] ).draw( false );
        console.log("Finished loading winsync replication agreements.");
      }
    });
  } // suffix
}


function get_and_set_repl_agmts () {
  /*
   * Get the replication agreements for the selected suffix
   */
  var suffix = $("#select-repl-agmt-suffix").val();

  if (suffix) {
    console.log("Loading replication agreements...");
    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'list', '--suffix=' + suffix ];
    log_cmd('get_and_set_repl_agmts', 'Get replication agreements', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      repl_agmt_table.clear().draw();
      var obj = JSON.parse(data);
      for (var idx in obj['items']) {
        agmt_attrs = obj['items'][idx]['attrs'];
        var agmt_name = agmt_attrs['cn'][0];
        var state = "Enabled";
        var update_status = "";
        var agmt_init_status = "Initialized";

        // Compute state (enabled by default)
        if ('nsds5replicaenabled' in agmt_attrs) {
          if (agmt_attrs['nsds5replicaenabled'][0].toLowerCase() == 'off'){
            state = "Disabled";
          }
        }

        // Check for status msgs
        if ('nsds5replicalastupdatestatus' in agmt_attrs) {
          update_status = agmt_attrs['nsds5replicalastupdatestatus'][0];
        }
        if ('nsds5replicalastinitstatus' in agmt_attrs &&
            agmt_attrs['nsds5replicalastinitstatus'][0] != "")
        {
          agmt_init_status = agmt_attrs['nsds5replicalastinitstatus'][0];
          if (agmt_init_status == "Error (0) Total update in progress" ||
              agmt_init_status == "Error (0)")
          {
            agmt_init_status = progress_html;
            var interval_agmt_name = agmt_name;
            var init_idx = agmt_init_counter;
            agmt_init_counter += 1;
            agmt_init_intervals[init_idx] = setInterval( do_agmt_init, 2000, suffix, interval_agmt_name, init_idx);
          } else if (agmt_init_status == "Error (0) Total update succeeded") {
            agmt_init_status = "Initialized";
          }
        } else if (agmt_attrs['nsds5replicalastinitstart'][0] == "19700101000000Z"){
          agmt_init_status = "Not initialized";
        }

        // Update table
        repl_agmt_table.row.add( [
          agmt_attrs['cn'][0],
          agmt_attrs['nsds5replicahost'][0],
          agmt_attrs['nsds5replicaport'][0],
          state,
          update_status,
          agmt_init_status,
          agmt_action_html
        ] ).draw( false );
      }
      console.log("Finished loading replication agreements.");
    }).fail(function () {
      repl_agmt_table.clear().draw();
    });
  } // suffix

  // Load fractional replication agreement attr list here
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket', 'schema', 'list'];
  log_cmd('get_and_set_repl_agmts', 'Get all schema objects', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(schema_data) {
    var schema_json = JSON.parse(schema_data);
    load_schema_objects_to_select('attributetypes', 'select-attr-list', schema_json);
  }).fail(function(oc_data) {
      console.log("Get all schema objects failed: " + oc_data.message);
      check_inst_alive(1);
  });
}


function get_and_set_cleanallruv() {
  console.log("Loading replication tasks...");
  let cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-tasks', 'list-cleanruv-tasks'];
  log_cmd('get_and_set_cleanallruv', 'Get the cleanAllRUV tasks', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    var tasks = JSON.parse(data);
    repl_clean_table.clear().draw();
    for (var idx in tasks['items']) {
      task_attrs = tasks['items'][idx]['attrs'];
      // Update table
      var abort_btn = cleanallruv_action_html;
      if (task_attrs['nstaskstatus'][0].includes('Successfully cleaned rid') ){
        abort_btn = "<i>Task Complete</i>";
      } else if (task_attrs['nstaskstatus'][0].includes('Task aborted for rid') ){
         abort_btn = "<i>Task Aborted</i>";
      }
      repl_clean_table.row.add( [
          task_attrs['cn'][0],
          get_date_string(task_attrs['createtimestamp'][0]),
          task_attrs['replica-base-dn'][0],
          task_attrs['replica-id'][0],
          task_attrs['nstaskstatus'][0],
          abort_btn
        ] );
    }
    repl_clean_table.draw(false);
    console.log("Finished loading replication tasks.");
  });
}


function get_and_set_repl_config () {
  var suffix = $("#select-repl-cfg-suffix").val();

  if (suffix) {
    $("#nsds5replicaid").css("border-color", "initial");
    repl_config_values = {};
    console.log("Loading replication configuration...");

    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'get', '--suffix=' + suffix ];
    log_cmd('get_and_set_repl_config', 'Get replication configuration', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      var repl = JSON.parse(data);
      var repl_type;
      var repl_flags;
      var manager = false;
      $('#repl-mgr-table').find("tr:gt(0)").remove();
      $(".ds-cfg").val("");
      $("#nsds5replicaprecisetombstonepurging").prop('checked', false);

      // Set configuration and the repl manager table
      for (var attr in repl['attrs']) {
        var vals = repl['attrs'][attr];
        attr = attr.toLowerCase();

        if (attr in repl_attr_map) {
          if (attr == "nsds5replicabinddn") {
            // update manager table
            for (var val_idx in vals){
              add_repl_mgr(vals[val_idx]);
              manager = true;
            }
          } else if (attr == "nsds5replicatype") {
            repl_type = vals[0];
          } else if (attr == "nsds5flags") {
            repl_flags = vals[0];
          } else {
            // Regular attribute
            if (vals[0] == "on") {
              $("#" + attr).prop('checked', true);
              $("#" + attr).trigger('change');
            } else if (vals[0] == "off") {
              $("#" + attr).prop('checked', false);
              $("#" + attr).trigger('change');
            }
            $("#" + attr ).val(vals[0]);
            repl_config_values[attr] = vals[0];
          }
        }
      }
      if (!manager) {
        // Add an empty row to define the table
        $("#repl-mgr-table tbody").append(
		      "<tr>"+
		      "<td class='ds-td'>None</td>"+
          "<td></td>" +
          "</tr>");
      }

      // Set the replica role
      if (repl_type == "3"){
        $("#select-repl-role").val("Master");
        current_role = "Master";
        $("#nsds5replicaid").show();
        $("#replicaid-label").show();
      } else {
        $("#nsds5replicaid").hide();
        $("#replicaid-label").hide();
        if (repl_flags == "1"){
          $("#select-repl-role").val("Hub");
          current_role = "Hub";
        } else {
          $("#select-repl-role").val("Consumer");
          current_role = "Consumer";
        }
      }
      current_rid = $("#nsds5replicaid").val();

      // Show the page (in case it was hidden)
      $("#ds-repl-enabled").hide();
      $("#repl-config-content").show();
      load_repl_suffix_dropdowns();

      console.log("Finished loading replication configuration.");
    }).fail(function(data) {
      // No replication
      current_role = "Disabled";
      $("#repl-config-content").hide();
      $("#ds-repl-enabled").show();
      load_repl_suffix_dropdowns();
    });
  } else {
    // No Suffix
    $("#repl-config-content").hide();
    $("#ds-repl-enabled").hide();
  }

  // Do the changelog settings
  $("#cl-create-div").show();
  $("#cl-del-div").hide();
  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'get-changelog'];
  log_cmd('get_and_set_repl_config', 'Get replication changelog', cmd);
  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
    $(".ds-cl").val("");  // Clear form
    var cl = JSON.parse(data);
    repl_cl_values = {};
    for (var attr in cl['attrs']) {
      var val = cl['attrs'][attr][0];
      attr = attr.toLowerCase();
      $("#" + attr ).val(val);
      $("#cl-create-div").hide();
      $("#cl-del-div").show();
      repl_cl_values[attr] = val;
    }
  }).fail(function() {
    // No changelog, clear the form
    $(".ds-cl").val("");
  });
}


function save_repl_config(suffix, ignore_rid) {
  /*
   * Check for changes in the replication settings
   */
  var set_repl_values = {};
  var set_cl_values = {};
  var arg_list = [];
  for (var attr in repl_attrs) {
    attr = repl_attrs[attr];
    var val = "";
    if ( $("#" + attr).is(':checkbox')) {
      if ( $("#" + attr).is(":checked")) {
        val = "on";
      } else {
        val = "off";
      }
    } else {
      val = $("#" + attr).val();
    }
    var prev_val = "";

    if (attr in repl_config_values) {
      prev_val = repl_config_values[attr];
    }

    if (val != prev_val) {
      if (attr == "nsds5replicaid"){
        // skip it since we are doing a promotion
        continue;
      }
      // Handle checkbox input
      if ( $("#" + attr).is(':checkbox')) {
        if ( $("#" + attr).is(":checked")) {
          arg_list.push(repl_attr_map[attr] + "=" + "on" );
        } else {
          // Not checked
          arg_list.push(repl_attr_map[attr] + "=" + "off" );
        }
      } else {
        // Regular input
        if (val != "") {
          // Regular setting - add to the list
          arg_list.push(repl_attr_map[attr] + "=" + val );
        } else {
          // removed
          arg_list.push(repl_attr_map[attr] + "=");
        }
      }
      set_repl_values[attr] = val;
    }
  }

  /*
   * Check for changes in the changelog settings
   */
  var arg_cl_list = [];
  for (var attr in repl_cl_attrs) {
    attr = repl_cl_attrs[attr];
    var val = $("#" + attr).val();
    var prev_val = "";
    if (attr in repl_cl_values) {
      prev_val = repl_cl_values[attr];
    }
    if (val != prev_val) {
      // we have a difference
      if (val != "") {
        // Regular setting -add to the list
        arg_cl_list.push(repl_attr_map[attr] + "=" + val);
      } else {
        // removed
        arg_cl_list.push(repl_attr_map[attr] + "=");
      }
      set_cl_values[attr] = val;
    }
  }

  /*
   * Save repl config settings
   */
  if (arg_list.length > 0){

    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'set', '--suffix=' + suffix ];
    cmd = cmd.concat(arg_list);
    log_cmd('save_repl_config', 'Set replication configuration', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      popup_success('Saved replication configuration');
      for (var key in set_repl_values) {
        // Update current in memory values
        repl_config_values[key] = set_repl_values[key];
      }
      /*
       * Save changelog settings
       */
      if (arg_cl_list.length > 0){
        var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication' ,'set-changelog'];
        cmd = cmd.concat(arg_cl_list);
        log_cmd('save_repl_config', 'Set changelog configuration', cmd);
        cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
          popup_success('Saved changelog configuration');
          for (var key in set_cl_values) {
            // Update current in memory values
            repl_cl_values[key] = set_cl_values[key];
          }
          get_and_set_repl_config();
        }).fail(function(data) {
          get_and_set_repl_config();
          popup_err("Failed to save changelog configuration", data.message);
        });
      } else {
        // No changelog changes, so we're done, refresh the settings
        get_and_set_repl_config();
      }
    }).fail(function(data) {
      // Restore prev values
      get_and_set_repl_config();
      popup_err("Failed to set replication configuration", data.message);
    });
  } else if (arg_cl_list.length > 0) {
    /*
     * Only changelog settings need to be applied
     */
    var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication' ,'set-changelog'];
    cmd = cmd.concat(arg_cl_list);
    log_cmd('save_repl_config', 'Set changelog configuration', cmd);
    cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
      popup_success('Saved changelog configuration');
      for (var key in set_cl_values) {
        // Update current in memory values
        repl_cl_values[key] = set_cl_values[key];
      }
      get_and_set_repl_config();
    }).fail(function(data) {
      get_and_set_repl_config();
      popup_err("Failed to save changelog configuration", data.message);
    });
  }
}


/*
 * Load the replication page, and set the handlers
 */
$(document).ready( function() {
  $("#replication-content").load("replication.html", function () {
        // Set up agreement table
    repl_agmt_table = $('#repl-agmt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No agreements configured",
        "search": "Search Agreements"
      },
      "columnDefs": [ {
        "targets": 6,
        "orderable": false
      } ],
      "columns": [
        { "width": "20%" },
        { "width": "20%" },
        { "width": "50px" },
        { "width": "50px" },
        { "width": "20%" },
        { "width": "20%" },
        { "width": "130px" }
      ],
    });

    // Set up windows sync agreement table
    repl_winsync_agmt_table = $('#repl-winsync-agmt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No winsync agreements configured",
        "search": "Search Agreements"
      },
      "columnDefs": [ {
        "targets": 6,
        "orderable": false
      } ]
    });

    // Set up CleanAllRUV Table
    repl_clean_table = $('#repl-clean-table').DataTable( {
      "paging": true,
      "searching": false,
      "bAutoWidth": false,
      "dom": 'B<"pull-left"f><"pull-right"l>tip',
      buttons: [
            {
                text: 'Refresh Task List',
                action: function ( e, dt, node, config ) {
                  console.log("hmmm");
                  get_and_set_cleanallruv();
                }
            }
        ],
      "lengthChange": false,
      "language": {
        "emptyTable": "No CleanAllRUV tasks",
      },
      "columnDefs": [ {
        "targets": 5,
        "orderable": false
      } ]
    });

    binddn_list_color = $("#repl-managers-list").css("border-color");

    // Load existing replication config (if any), set role, etc

    $("#schedule-settings").hide();

    $("#repl-config-btn").on("click", function() {
      $(".all-pages").hide();
      $("#replication-content").show();
      $("#repl-config").show();
    });

    $("#repl-agmts-btn").on("click", function() {
      $(".all-pages").hide();
      $("#replication-content").show();
      $("#repl-agmts").show();
    });

    $("#repl-winsync-btn").on("click", function() {
      $(".all-pages").hide();
      $("#replication-content").show();
      $("#repl-winsync").show();
    });
    $("#repl-tasks-btn").on("click", function() {
      $(".all-pages").hide();
      $("#replication-content").show();
      $("#repl-cleanallruv").show();
    });

    $("#select-repl-cfg-suffix").on("change", function() {
      get_and_set_repl_config();
    });

    $("#select-repl-agmt-suffix").on("change", function() {
      get_and_set_repl_agmts();
    });

    $("#select-repl-winsync-suffix").on("change", function() {
      get_and_set_repl_winsync_agmts();
    });

    $("#select-repl-role").on("change", function() {
      var new_role = $(this).val();
      if (new_role == "Master"){
        if (current_role != new_role) {
          // Reset replica ID for a new master
          $("#nsds5replicaid").val("0");
        }
        $("#nsds5replicaid").show();
        $("#replicaid-label").show();
        $("#repl-config-content").show();
      } else {
        $("#nsds5replicaid").hide();
        $("#replicaid-label").hide();
        $("#repl-config-content").show();
        $("#nsds5replicaid").css("border-color", "initial");
      }
    });

    $("#enable-repl-btn").on('click', function () {
      clear_enable_repl_form();
    });

    /*
     * Enable replication - select role dynamics
     */
    $("#select-enable-repl-role").on("change", function() {
      var new_role = $(this).val();
      if (new_role == "Master"){
        $("#repl-rid-form").show();
      } else {
        $("#repl-rid-form").hide();
      }
    });
    /*
     * Enable replication - save it
     */
    $("#enable-repl-save").on('click', function () {
      var suffix = $("#select-repl-cfg-suffix").val();
      var role = $("#select-enable-repl-role").val();
      var repl_dn = $("#enable-repl-mgr-dn").val();
      var repl_pw = $("#enable-repl-pw").val();
      var repl_pw_confirm = $("#enable-repl-pw-confirm").val();
      var repl_group = $("#enable-bindgroupdn").val();
      var cmd = [];

      // Validate all the inputs
      if (role == "Master") {
        // Master role, get and valid replica id
        var rid = $("#nsds5replicaid-form").val();
        if (rid == ""){
          $("#nsds5replicaid-form").css("border-color", "red");
          popup_msg("Missing Replica ID",
                    "A Master replica requires a unique identifier.  " +
                    "Please enter a value for <b>Replica ID</b> between 1 and 65534");
          return;
        }
        if (valid_num(rid)){
          if (rid < 1 || rid >= 65535){
            $("#nsds5replicaid-form").css("border-color", "red");
            popup_msg("Invalid Replica ID",
                      "A Master replica requires a unique identifier.  " +
                      "Please enter a value for <b>Replica ID</b> between 1 and 65534");
            return;
          }
        } else {
          $("#nsds5replicaid-form").css("border-color", "red");
          popup_msg("Replica ID is not a number",
                    "A Master replica requires a unique identifier.  " +
                    "Please enter a value for <b>Replica ID</b> between 1 and 65534");
          return;
        }
        $("#nsds5replicaid-form").css("border-color", "initial");
        cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'enable',
               '--suffix=' + suffix, '--role=' + role, '--replica-id=' + rid];
      } else {
        // Hub or Consumer - must have a bind dn/group
        if (repl_dn == "" && repl_group == ""){
          popup_msg("Attention!", "Replication Manager or Replication Bind Group is required");
          return;
        }
        cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'enable',
               '--suffix=' + suffix, '--role=' + role];
      }

      if (repl_dn != ''){
        if (repl_pw != repl_pw_confirm) {
          popup_msg("Attention!", "Passwords do not match");
          $("#enable-repl-pw").val("");
          $("#enable-repl-pw-confirm").val("");
          return;
        }
        if (!valid_dn(repl_dn)) {
          popup_msg("Attention!", "Invalid DN for Replication Manager");
          return;
        }
        cmd.push.apply(cmd, ['--bind-dn=' + repl_dn]);
        cmd.push.apply(cmd, ['--bind-passwd=' + repl_pw]);
      }
      if (repl_group != ""){
        if (!valid_dn(repl_group)){
          popup_msg("Attention!", "Invalid DN for Replication Bind Group");
          return;
        }
        cmd.push.apply(cmd, ['--bind-group-dn=' + repl_group]);
      }

      // Enable replication finally
      log_cmd('#enable-repl-save (click)', 'Enable replication', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        // Replication has been enabled
        popup_success("Successfully enabled replication");
        $("#enable-repl-form").modal('toggle');
        get_and_set_repl_config();
      }).fail(function(data) {
        // Undo what we have done
        popup_err("Failed to enable replication", data.message);
        var disable_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'disable', '--suffix=' + suffix ];
        log_cmd('#enable-repl-save (click)', 'Disable replication after error', disable_cmd);
        cockpit.spawn(disable_cmd, { superuser: true, "err": "message", "environ": [ENV]}).always(function(data) {
          get_and_set_repl_config();
          $("#enable-repl-form").modal('toggle');
        });
      });
    });

    /*
     * Disable replication
     */
    $("#disable-repl-btn").on('click', function () {
      var suffix = $("#select-repl-cfg-suffix").val();
      popup_confirm("Are you sure you want to disable replication?  This will remove all your replication agreements and can not be undone!", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'disable', '--suffix=' + suffix ];
          log_cmd('#disable-repl-btn (click)', 'Disable replication', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            current_role = "Disabled";
            $("#repl-config-content").hide();
            popup_success('Successfully disabled replication');
            get_and_set_repl_config();
          }).fail(function(data) {
            popup_err("Failed to disable replication", data.message);
            get_and_set_repl_config();
          });
        }
      });
    });

    /*
     * Save replication configuration
     */
    $("#repl-config-save").on('click', function () {
      var suffix = $("#select-repl-cfg-suffix").val();
      var rid = $("#nsds5replicaid").val();

      if (suffix) {
        /*
         * Did we config change, promote, or demote this replica?
         */
        var new_role = $("#select-repl-role").val();
        if (new_role != current_role) {
          /*
           * Promote/demote the replica
           */
          popup_confirm("Are you sure you want to change the <i>replication role</i> to \"<b>" + new_role + "</b>\"?", "Confirmation", function (yes) {
            if (yes) {
              if (new_role == "Master"){
                /*
                 * Promote to Master
                 */
                if ( !valid_num(rid) ) {
                  popup_msg("Invalid Replica ID",
                            "A Master replica requires a unique numerical identifier.  Please enter a value for <b>Replica ID</b> between 1 and 65534");
                  get_and_set_repl_config();
                  return;
                }
                var rid_num = parseInt(rid, 10);
                if (rid_num < 1 || rid_num >= 65535){
                  popup_msg("Missing Required Replica ID",
                            "A Master replica requires a unique numerical identifier.  Please enter a value for <b>Replica ID</b> between 1 and 65534");
                  get_and_set_repl_config();
                  return;
                }
                var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'promote',
                           '--suffix=' + suffix, "--newrole=" + new_role, "--replica-id=" + rid];
                log_cmd('#repl-config-save (click)', 'Promote replica to Master', cmd);
                cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                  current_role = "Master";;
                  popup_success('Successfully promoted replica to a <b>Master</b>');
                  get_and_set_repl_config();
                  save_repl_config(suffix, true);
                }).fail(function(data) {
                  popup_err("Failed to promote replica to a Master", data.message);
                  get_and_set_repl_config();
                });
              } else if (new_role == "Hub" && current_role == "Master"){
                /*
                 * Demote to Hub, but first check that we have a replication manager
                 */
                var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'get', '--suffix=' + suffix ];
                log_cmd('get_and_set_repl_config', 'Get replication configuration', cmd);
                cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                  var repl = JSON.parse(data);
                  var manager = false;
                  for (var attr in repl['attrs']) {
                    if (attr.toLowerCase() == "nsds5replicabinddn") {
                      manager = true;
                      break;
                    }
                  }
                  if (manager == false) {
                    popup_msg("Missing Required Setting",
                              "You must create a replication manager before you can demote this replica to a Hub");
                    return;
                  }
                  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'demote',
                             '--suffix=' + suffix, "--newrole=" + new_role];
                  log_cmd('#repl-config-save (click)', 'Demote replica to Hub', cmd);
                  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                    current_role = "Hub";
                    popup_success('Successfully demoted replica to a <b>Hub</b>');
                    save_repl_config(suffix, true);
                  }).fail(function(data) {
                    popup_err("Failed to demote replica to a Hub", data.message);
                    get_and_set_repl_config();
                  });
                });
              } else if (new_role == "Hub" && current_role == "Consumer"){
                /*
                 * Promote to Hub
                 */
                var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'promote',
                           '--suffix=' + suffix, "--newrole=" + new_role];
                log_cmd('#repl-config-save (click)', 'Promote replica to Hub ', cmd);
                cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                  current_role = "Hub";;
                  popup_success('Successfully promoted replica to a <b>Hub</b>');
                  save_repl_config(suffix, true);
                }).fail(function(data) {
                  popup_err("Failed to promote replica to a Hub", data.message);
                  get_and_set_repl_config();
                });
              } else {
                /*
                 * Demote to Consumer, but first confirm we have a replication manager
                 */
                var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'get', '--suffix=' + suffix ];
                log_cmd('get_and_set_repl_config', 'Get replication configuration', cmd);
                cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                  var repl = JSON.parse(data);
                  var manager = false;
                  for (var attr in repl['attrs']) {
                    if (attr.toLowerCase() == "nsds5replicabinddn") {
                      manager = true;
                      break;
                    }
                  }
                  if (manager == false) {
                    popup_msg("Missing Required Setting",
                              "You must create a replication manager before you can demote this replica to a Consumer.");
                    return;
                  }
                  var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'demote',
                             '--suffix=' + suffix, "--newrole=" + new_role];
                  log_cmd('#repl-config-save (click)', 'Demote replication to Consumer', cmd);
                  cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
                    current_role = "Consumer";
                    popup_success('Successfully demoted replica to a <b>Consumer</b>');
                    save_repl_config(suffix, true);
                  }).fail(function(data) {
                    popup_err("Failed to demote replica to a Consumer", data.message);
                    get_and_set_repl_config();
                  });
                });
              }
            } else {
              // Not changing the role - reset the dropdown
              $("#select-repl-role").val(current_role);
              get_and_set_repl_config();
            }
          }); // popup_confirm
        } else {
          /*
           * We did NOT promote/demote, etc.  This was just a configuration change...
           */
          save_repl_config(suffix, false);
        }
      } // Suffix
    });

    // Create changelog
    $("#create-cl-btn").on('click', function () {
      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication' ,'create-changelog'];
      log_cmd('#create-cl-btn (click)', 'Create replication changelog', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        get_and_set_repl_config();
        popup_success('Successfully created replication changelog');
      }).fail(function(data) {
        get_and_set_repl_config();
        popup_err("Failed to create replication changelog", data.message);
      });
    });

    // Remove changelog
    $("#delete-cl-btn").on('click', function () {
      popup_confirm("Are you sure you want to delete the replication changelog as it will break all the existing agreements?", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication' ,'delete-changelog'];
          log_cmd('#delete-cl-btn (click)', 'Delete replication changelog', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            get_and_set_repl_config();
            popup_success('Successfully removed replication changelog');
          }).fail(function(data) {
            get_and_set_repl_config();
            popup_err("Failed to remove replication changelog", data.message);
          });
        }
      });
    });

    // Save Repl Agreement Wizard
    $("#agmt-save").on("click", function() {
      // Get all the settings
      var suffix = $("#select-repl-agmt-suffix").val();
      var cmd = [];
      var cmd_args = [];
      var param_err = false;
      var agmt_name = $("#agmt-cn").val();
      var agmt_host = $("#nsds5replicahost").val();
      var agmt_port = $("#nsds5replicaport").val();
      var agmt_bind = $("#nsds5replicabinddn").val();
      var agmt_bindpw = $("#nsds5replicacredentials").val();
      var agmt_bindpw_confirm = $("#nsds5replicacredentials-confirm").val();
      var agmt_conn = $("#nsds5replicatransportinfo").val();
      var agmt_method = $("#nsds5replicabindmethod").val();
      var agmt_schedule = "";
      var agmt_init = $("#init-options").val();
      var agmt_exclude = "";
      var agmt_tot_exclude = "";
      var agmt_strip = "";
      var editing = false;
      var init_replica = false;

      if ($("#agmt-wizard-title").text().includes('Edit') ) {
        editing = true;
      }

      // Check required settings
      if ( agmt_port == "") {
        $("#nsds5replicaport").css("border-color", "red");
        param_err = true;
      } else {
        $("#nsds5replicaport").css("border-color", "initial");
        cmd_args.push("--port=" + agmt_port);
      }
      if ( agmt_host == "") {
        $("#nsds5replicahost").css("border-color", "red");
        param_err = true;
      } else {
        $("#nsds5replicahost").css("border-color", "initial");
        cmd_args.push('--host=' + agmt_host);
      }
      if ( agmt_conn == "") {
        $("#nsds5replicatransportinfo").css("border-color", "red");
        param_err = true;
      } else {
        $("#nsds5replicatransportinfo").css("border-color", "initial");
        cmd_args.push('--conn-protocol=' + agmt_conn);
      }
      if ( agmt_method == "") {
        $("#nsds5replicabindmethod").css("border-color", "red");
        param_err = true;
      } else {
        $("#nsds5replicabindmethod").css("border-color", "initial");
        cmd_args.push('--bind-method=' + agmt_method);
      }
     if ( agmt_bind == "") {
        $("#nsds5replicabinddn").css("border-color", "red");
        param_err = true;
      } else {
        $("#nsds5replicabinddn").css("border-color", "initial");
        cmd_args.push('--bind-dn=' + agmt_bind);
      }
      if (param_err ){
        popup_msg("Error", "Missing required parameters");
        return;
      }

      /*
       * Handle the optional settings
       */
      $("#frac-exclude-list option").each(function() {
        agmt_exclude += $(this).val() + " ";
      });
      $("#frac-exclude-tot-list option").each(function() {
        agmt_tot_exclude += $(this).val() + " ";
      });
      $("#frac-strip-list option").each(function() {
        agmt_strip += $(this).val() + " ";
      });

      if (agmt_bindpw != agmt_bindpw_confirm) {
        popup_msg("Attention!", "Passwords do not match");
        return;
      }

      // Bind Password
      if (!editing ){
        if (agmt_bindpw != "") {
          cmd_args.push('--bind-passwd=' + agmt_bindpw);
        }
      } else {
        if ( !('nsds5replicacredentials' in repl_agmt_values) ||
             agmt_bindpw != repl_agmt_values['nsds5replicacredentials'])
        {
          cmd_args.push('--bind-passwd=' + agmt_bindpw);
        }
      }
      // Frac attrs
      agmt_exclude = agmt_exclude.trim();
      if (!editing) {
        if (agmt_exclude != "") {
          cmd_args.push('--frac-list='+ agmt_exclude);
        }
      } else {
        if ( !('nsds5replicatedattributelist' in repl_agmt_values) ||
            agmt_exclude != repl_agmt_values['nsds5replicatedattributelist'].replace(frac_prefix, ""))
        {
          cmd_args.push('--frac-list=' + frac_prefix + ' ' + agmt_exclude);
        }
      }
      // Frac total attr
      agmt_tot_exclude = agmt_tot_exclude.trim();
      if (!editing) {
        if (agmt_tot_exclude != "") {
          cmd_args.push('--frac-list-total='+ agmt_tot_exclude);
        }
      } else {
        if ( !('nsds5replicatedattributelisttotal' in repl_agmt_values) ||
             agmt_tot_exclude != repl_agmt_values['nsds5replicatedattributelisttotal'].replace(frac_prefix, ""))
        {
          cmd_args.push('--frac-list-total=' + frac_prefix + ' ' + agmt_tot_exclude);
        }
      }
      // Strip attrs
      agmt_strip = agmt_strip.trim();
      if (!editing) {
        if (agmt_strip != "") {
          cmd_args.push('--strip-list='+ agmt_strip);
        }
      } else {
        if ( !('nsds5replicastripattrs' in repl_agmt_values) ||
             agmt_strip != repl_agmt_values['nsds5replicastripattrs'] )
        {
          cmd_args.push('--strip-list='+ agmt_strip);
        }
      }

      if ( !($("#agmt-schedule-checkbox").is(":checked")) ){
        agmt_start = $("#agmt-start-time").val().replace(':','');
        agmt_end = $("#agmt-end-time").val().replace(':','');

        if (agmt_start == agmt_end) {
          popup_msg("Error", "The replication start and end times can not behte same");
          return;
        }

        // build the days
        var agmt_days = "";
        if ( $("#schedule-sun").is(":checked") ){
          agmt_days =  "0";
        }
        if ( $("#schedule-mon").is(":checked") ){
          agmt_days += "1";
        }
        if ( $("#schedule-tue").is(":checked") ){
          agmt_days += "2";
        }
        if ( $("#schedule-wed").is(":checked") ){
          agmt_days += "3";
        }
        if ( $("#schedule-thu").is(":checked") ){
          agmt_days += "4";
        }
        if ( $("#schedule-fri").is(":checked") ){
          agmt_days += "5";
        }
        if ( $("#schedule-sat").is(":checked") ){
          agmt_days += "6";
        }
        if (agmt_days == "" ){
          popup_msg("Error", "You must set at least one day in the schedule to perform replication");
          return;
        }
        // Set final value
        agmt_schedule = agmt_start + "-" + agmt_end + " " + agmt_days;

        if (!editing ){
          cmd_args.push('--schedule=' + agmt_schedule);
        } else {
          if ( !('nsds5replicaupdateschedule' in repl_agmt_values) ||
               agmt_schedule != repl_agmt_values['nsds5replicaupdateschedule'] )
          {
            cmd_args.push('--schedule=' + agmt_schedule);
          }
        }
      } else {
        // if "sync all the time" is checked, might need to remove the schedule attribute
        if ('nsds5replicaupdateschedule' in repl_agmt_values) {
          cmd_args.push('--schedule=');
        }
      }
      if (agmt_init == "online-init") {
        init_replica = true;
      }
      if ( agmt_name == "") {
        $("#agmt-cn").css("border-color", "red");
        param_err = true;
      } else {
        $("#agmt-cn").css("border-color", "initial");
        cmd_args.push('"' + agmt_name + '"');
      }

      // Create agreement in DS
      if ( editing ) {
        cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'set', '--suffix=' + suffix ];
      } else {
        cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'create', '--suffix=' + suffix];
      }
      cmd = cmd.concat(cmd_args);
      log_cmd('#agmt-save (click)', 'Create/Set replication agreement', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        if (editing){
          popup_success('Successfully edited replication agreement');
        } else {
          popup_success('Successfully created replication agreement');
        }
        if (init_replica) {
          // Launch popup stating initialization has begun
          var init_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'init', '--suffix=' + suffix, agmt_name ];
          log_cmd('#agmt-save (click)', 'Initialize agreement', init_cmd);
          cockpit.spawn(init_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
            get_and_set_repl_agmts();
          }).fail(function(data) {
            popup_err("Failed to initialize replication agreement", data.message);
          });
        } else {
          get_and_set_repl_agmts();
        }
      }).fail(function(data) {
        if (editing) {
         popup_err("Failed to edit replication agreement", data.message);
       } else {
         popup_err("Failed to create replication agreement", data.message);
       }
      });

      // Done, close the form
      $("#agmt-form").modal('toggle');
      clear_agmt_wizard();
    });

    /*
     * Initialize agreement
     */
    $(document).on('click', '.agmt-init-btn', function(e) {
      e.preventDefault();
      var suffix = $("#select-repl-agmt-suffix").val();
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var agmt_name = data[0];
      var row_idx = $(this).closest('tr').index();
      repl_agmt_table.cell({row: row_idx, column: 5}).data(progress_html).draw();

      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'init', '--suffix=' + suffix, '"' + agmt_name + '"' ];
      log_cmd('.agmt-init-btn (click)', 'Initialize replication agreement', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        var init_idx = agmt_init_counter;
        agmt_init_counter += 1;
        agmt_init_intervals[init_idx] = setInterval( do_agmt_init, 2000, suffix, agmt_name, init_idx);
      }).fail(function(data) {
        get_and_set_repl_agmts();
        popup_err("Failed to initialize agreement", data.message);
      });
    });

    $(document).on('click', '.winsync-agmt-init-btn', function(e) {
      e.preventDefault();
      var suffix = $("#select-repl-winsync-suffix").val();
      var data = repl_winsync_agmt_table.row( $(this).parents('tr') ).data();
      var agmt_name = data[0];
      var row_idx = $(this).closest('tr').index();
      repl_winsync_agmt_table.cell({row: row_idx, column: 5}).data(progress_html).draw();

      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'init', '--suffix=' + suffix, '"' + agmt_name + '"' ];
      log_cmd('.winsync-agmt-init-btn (click)', 'Initialize replication winsync agreement', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        var init_idx = agmt_init_counter;
        agmt_init_counter += 1;
        agmt_init_intervals[init_idx] = setInterval( do_winsync_agmt_init, 2000, suffix, agmt_name, init_idx);
      }).fail(function(data) {
        get_and_set_repl_winsync_agmts();
        popup_err("Failed to initialize winsync agreement", data.message);
      });
    });


    /* Store the repl dn from the table when opening the mgr delete confirmation modal */
    $(document).on('click', '.remove-repl-mgr', function(e) {
      e.preventDefault();
      var mgr_row =  $(this).parent().parent();
      var suffix = $("#select-repl-cfg-suffix").val();
      var mgr_dn = mgr_row.children("td:nth-child(1)");
      popup_confirm("Are you sure you want to delete Replication Manager:  <b>" + mgr_dn.text() + "<b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication' ,'set',
                     '--repl-del-bind-dn=' + mgr_dn.text(), '--suffix=' + suffix];
          log_cmd('.remove-repl-mgr (click)', 'Remove replication manager ', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            /* Remove the manager entry */
            var del_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication',
                           'delete-manager', "--name=" + mgr_dn.text()];
            log_cmd('.remove-repl-mgr(click)', 'Delete replication manager entry', del_cmd);
            cockpit.spawn(del_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
              popup_success('Successfully removed replication manager');
              get_and_set_repl_config();
            });
          }).fail(function(data) {
            get_and_set_repl_config();
            popup_err("Failed to remove replication manager", data.message);
          });
        }
      });
    });

    /*
     * Delete repl agreement
     */
    $(document).on('click', '.agmt-del-btn', function(e) {
      e.preventDefault();
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var del_agmt_name = data[0];
      var agmt_row = $(this);
      popup_confirm("Are you sure you want to delete replication agreement: <b>" + del_agmt_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var suffix = $("#select-repl-agmt-suffix").val();
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'delete', '--suffix=' + suffix, '"' + del_agmt_name + '"'];
          log_cmd('.agmt-del-btn (click)', 'Delete replication agreement', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success('Successfully removed replication agreement');
            // Update table
            repl_agmt_table.row( agmt_row.parents('tr') ).remove().draw( false );
          }).fail(function(data) {
            get_and_set_repl_config();
            popup_err("Failed to remove replication agreement", data.message);
          });
        }
      });
    });

    $(document).on('click', '.winsync-agmt-del-btn', function(e) {
      e.preventDefault();
      var data = repl_winsync_agmt_table.row( $(this).parents('tr') ).data();
      var del_agmt_name = data[0];
      var agmt_row = $(this);
      popup_confirm("Are you sure you want to delete replication agreement: <b>" + del_agmt_name + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var suffix = $("#select-repl-agmt-suffix").val();
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'delete', '--suffix=' + suffix, '"' + del_agmt_name + '"'];
          log_cmd('.winsync-agmt-del-btn (click)', 'Delete replication winsync agreement', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success('Successfully removed replication winsync agreement');
            // Update table
            repl_winsync_agmt_table.row( agmt_row.parents('tr') ).remove().draw( false );
          }).fail(function(data) {
            get_and_set_repl_config();
            popup_err("Failed to remove replication winsync agreement", data.message);
          });
        }
      });
    });

    /*
     * Edit Agreement
     */
    $(document).on('click', '.agmt-edit-btn', function(e) {
      e.preventDefault();
      clear_agmt_wizard();
      var suffix = $("#select-repl-agmt-suffix").val();
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var edit_agmt_name = data[0];
      // Set agreement form values
      $("#agmt-wizard-title").html("<b>Edit Replication Agreement</b>");

      // Hide init dropdown
      $("#init-agmt-dropdown").hide();

      // Get agreement from DS and populate form
      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'get', '--suffix=' + suffix, '"' + edit_agmt_name + '"'];
      log_cmd('.agmt-edit-btn (click)', 'Edit replication agreement', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        var agmt_obj = JSON.parse(data);
        var frac_attrs = "";
        var frac_tot_attrs = "";
        var strip_attrs = "";

        $("#agmt-cn").val(edit_agmt_name);
        for (var attr in agmt_obj['attrs']) {
          var val = agmt_obj['attrs'][attr][0];
          attr = attr.toLowerCase();
          $("#" + attr).val(val);
          repl_agmt_values[attr] = val;
        }

        // Fill Password Confirm Input
        if ( 'nsds5replicacredentials' in agmt_obj['attrs'] ){
          $("#nsds5replicacredentials-confirm").val(agmt_obj['attrs']["nsds5replicacredentials"][0]);
        }

        // Transport info
        val = agmt_obj['attrs']["nsds5replicatransportinfo"][0].toLowerCase();
        if (val == "ldap"){
          $("#nsds5replicatransportinfo").val("LDAP");
        } else if (val == "ldaps"){
          $("#nsds5replicatransportinfo").val("LDAPS");
        } else if (val == "starttls" || val == "tls"){
          $("#nsds5replicatransportinfo").val("StartTLS");
        }

        // Bind Method
        val = agmt_obj['attrs']["nsds5replicabindmethod"][0].toLowerCase();
        if (val == "simple"){
          $("#nsds5replicabindmethod").val("SIMPLE");
        } else if (val == "sasl/digest-md5"){
          $("#nsds5replicabindmethod").val("SASL/DIGEST-MD5");
        } else if (val == "sasl/gssapi"){
          $("#nsds5replicabindmethod").val("SASL/GSSAPI");
        } else if (val == "sslclientauth"){
          $("#nsds5replicatransportinfo").val("SSLCLIENTAUTH");
        }

        // Load fractional lists
        if ( 'nsds5replicatedattributelist' in agmt_obj['attrs'] ){
          frac_attrs = agmt_obj['attrs']['nsds5replicatedattributelist'][0];
          frac_attrs = frac_attrs.replace(frac_prefix, "").split(" ");
          for(var i = 0; i < frac_attrs.length; i++) {
            var opt = frac_attrs[i];
            if (opt != "") {
              var option = $('<option></option>').attr("value", opt).text(opt);
              $("#frac-exclude-list").append(option);
            }
          }
        }
        if ( 'nsds5replicatedattributelisttotal' in agmt_obj['attrs'] ){
          frac_tot_attrs = agmt_obj['attrs']['nsds5replicatedattributelisttotal'][0];
          frac_tot_attrs = frac_tot_attrs.replace(frac_prefix, "").split(" ");
          for(var i = 0; i < frac_tot_attrs.length; i++) {
            var opt = frac_tot_attrs[i];
            if (opt != "") {
             var option = $('<option></option>').attr("value", opt).text(opt);
              $("#frac-exclude-tot-list").append(option);
            }
          }
        }
        if ( 'nsds5replicastripattrs' in agmt_obj['attrs'] ){
          strip_attrs = agmt_obj['attrs']['nsds5replicastripattrs'][0];
          strip_attrs = strip_attrs.split(" ");
          for(var i = 0; i < strip_attrs.length; i++) {
            var opt = strip_attrs[i];
            if (opt != "") {
              var option = $('<option></option>').attr("value", opt).text(opt);
              $("#frac-strip-list").append(option);
            }
          }
        }

        // Set schedule
        if ( 'nsds5replicaupdateschedule' in agmt_obj['attrs'] ){
          var val =  agmt_obj['attrs']['nsds5replicaupdateschedule'][0];
          var parts = val.split(" ");
          var days = parts[1];
          var times = parts[0].split("-");
          var start_time = times[0].substring(0,2) + ":" + times[0].substring(2,4);
          var end_time = times[1].substring(0,2) + ":" + times[1].substring(2,4);

          $("#agmt-schedule-checkbox").prop('checked', false);
          $('#agmt-schedule-panel *').attr('disabled', false);
          $("#schedule-settings").show();

          $("#agmt-start-time").val(start_time);
          $("#agmt-end-time").val(end_time);
          if ( days.indexOf('0') != -1){ // Sunday
            $("#schedule-sun").prop('checked', true);
          }
          if ( days.indexOf('1') != -1){ // Monday
            $("#schedule-mon").prop('checked', true);
          }
          if ( days.indexOf('2') != -1){ // Tuesday
            $("#schedule-tue").prop('checked', true);
          }
          if ( days.indexOf('3') != -1){ // Wednesday
            $("#schedule-wed").prop('checked', true);
          }
          if ( days.indexOf('4') != -1){ // Thursday
            $("#schedule-thu").prop('checked', true);
          }
          if ( days.indexOf('5') != -1){ // Friday
            $("#schedule-fri").prop('checked', true);
          }
          if ( days.indexOf('6') != -1){ // Saturday
            $("#schedule-sat").prop('checked', true);
          }
        }
        // Finally Open form
        $("#agmt-form").modal('toggle');
      }).fail(function(data) {
        popup_err("Failed to get replication agreement entry", data.message);
      });
    });

    /*
     * Edit Winsync Agreement
     */
    $(document).on('click', '.winsync-agmt-edit-btn', function(e) {
      e.preventDefault();
      clear_winsync_agmt_wizard();
      var suffix = $("#select-repl-winsync-suffix").val();
      var data = repl_winsync_agmt_table.row( $(this).parents('tr') ).data();
      var edit_agmt_name = data[0];

      // Get agreement from DS and populate form
      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'get', '--suffix=' + suffix, '"' + edit_agmt_name + '"'];
      log_cmd('.winsync-agmt-edit-btn (click)', 'Get replication winsync agreement', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        var agmt_obj = JSON.parse(data);

        // Set agreement form values
        $("#winsync-agmt-wizard-title").html("<b>Edit Winsync Agreement</b>");
        // Hide init dropdown
        $("#winsync-init-chbx").hide();
        $("#winsync-agmt-cn").val(edit_agmt_name);
        for (var attr in agmt_obj['attrs']) {
          var val = agmt_obj['attrs'][attr][0];
          attr = attr.toLowerCase();
          $("#winsync-" + attr).val(val);
          if (val == "on") {
            $("#winsync-" + attr + "-checkbox").prop('checked', true);
          } else if (val == "off") {
            $("#winsync-" + attr + "-checkbox").prop('checked', false);
          }
          repl_winsync_agmt_values[attr] = val;
        }

        // Fill Password Confirm Input
        if ( 'nsds5replicacredentials' in agmt_obj['attrs'] ){
          $("#winsync-nsds5replicacredentials-confirm").val(agmt_obj['attrs']["nsds5replicacredentials"][0]);
        }

        // Transport info
        val = agmt_obj['attrs']["nsds5replicatransportinfo"][0].toLowerCase();
        if (val == "ldap"){
          $("#winsync-nsds5replicatransportinfo").val("LDAP");
        } else if (val == "ldaps"){
          $("#winsync-nsds5replicatransportinfo").val("LDAPS");
        } else if (val == "starttls" || val == "tls"){
          $("#winsync-nsds5replicatransportinfo").val("StartTLS");
        }

        // Finally open the form
        $("#winsync-agmt-form").modal('toggle');
      }).fail(function(data) {
        popup_err("Failed to load replication winsync agreement entry", data.message);
      });
    });


    // Handle disabling/enabling of agmt schedule panel
    $('#agmt-schedule-panel *').attr('disabled', true); /// Disabled by default
    $("#agmt-schedule-checkbox").change(function() {
      if(this.checked) {
        $('#agmt-schedule-panel *').attr('disabled', true);
        $("#schedule-settings").hide();
      } else {
        $('#agmt-schedule-panel *').attr('disabled', false);
        $("#schedule-settings").show();
      }
    });

    // Based on the connection type change the agmt-auth options
    $("#nsds5replicatransportinfo").change(function() {
      var ldap_opts = {"Simple": "Simple",
                       "SASL/DIGEST-MD5": "SASL/DIGEST-MD5",
                       "SASL/GSSAPI": "SASL/GSSAPI"};
      var ldaps_opts = {"Simple": "Simple",
                        "SSL Client Authentication": "SSL Client Authentication",
                        "SASL/DIGEST-MD5": "SASL/DIGEST-MD5"};
      var $auth = $("#nsds5replicabindmethod");
      $auth.empty();
      var conn = $('#nsds5replicatransportinfo').val();
      if (conn == "LDAP"){
        $.each(ldap_opts, function(key, value) {
          $auth.append($("<option></option>").attr("value", value).text(key));
        });
      } else {
        // TLS options
        $.each(ldaps_opts, function(key, value) {
          $auth.append($("<option></option>").attr("value", value).text(key));
        });
      }
      $("#nsds5replicabinddn").prop('disabled', false);
      $("#nsds5replicacredentials").prop('disabled', false);
      $("#nsds5replicacredentials-confirm").prop('disabled', false);
    });

    // Check for auth changes and disable/enable bind DN & password
    $("#nsds5replicabindmethod").change(function() {
      var authtype = $('#nsds5replicabindmethod').val();
      if (authtype == "SSL Client Authentication") {
        $("#nsds5replicabinddn").prop('disabled', true);
        $("#nsds5replicacredentials").prop('disabled', true);
        $("#nsds5replicacredentials-confirm").prop('disabled', true);
      } else {
        $("#nsds5replicabinddn").prop('disabled', false);
        $("#nsds5replicacredentials").prop('disabled', false);
        $("#nsds5replicacredentials-confirm").prop('disabled', false);
      }
    });

    // Create time picker for agmt schedule (start end times))
    $('input.timepicker').timepicker({
      'timeFormat': 'H:i',
      'step': 15
    });

    // Accordion opening/closings

    $(".ds-accordion-panel").css('display','none');

    $("#repl-config-accordion").on("click", function() {
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

    $("#frac-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Fractional Settings ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Fractional Settings ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    $("#schedule-accordion").on("click", function() {
      this.classList.toggle("active");
      var panel = this.nextElementSibling;
      if (panel.style.display === "block") {
        var show = "&#9658 Show Schedule Settings ";
        $(this).html(show);
        panel.style.display = "none";
        $(this).blur();
      } else {
        var hide = "&#9660 Hide Schedule Settings ";
        $(this).html(hide);
        panel.style.display = "block";
        $(this).blur();
      }
    });

    /*
     * Handle the repl agmt wizard select lists
     */

    /*
     * Set the "select"'s list-id in a hidden field on the select attribute form
     * so we know what list to update after the selection
     */

    $(".ds-fractional-btn").on('click', function() {
      // reset the list
      $("#select-attr-list").prop('selectedIndex',-1);
    });
    $("#frac-list-add-btn").on('click', function () {
      $("#attr-form-id").val("frac-exclude-list");
    });
    $("#frac-total-list-add-btn").on('click', function () {
      $("#attr-form-id").val("frac-exclude-tot-list");
    });
    $("#frac-strip-list-add-btn").on('click', function () {
      $("#attr-form-id").val("frac-strip-list");
    });

    // Handle the attribute removal from the lists
    $("#frac-list-remove-btn").on("click", function () {
      $("#frac-exclude-list").find('option:selected').remove();
    });
    $("#frac-total-list-remove-btn").on("click", function () {
      $("#frac-exclude-tot-list").find('option:selected').remove();
    });
    $("#frac-strip-list-remove-btn").on("click", function () {
      $("#frac-strip-list").find('option:selected').remove();
    });

    // Update agmt form attribute selection lists
    $("#select-attr-save").on("click", function () {
      // Get the id from the hidden input filed and append the attribute to it
      var list_id = $("#attr-form-id").val();
      var add_attrs = $("#select-attr-list").find('option:selected');
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#' + list_id + ' option[value="' + add_attrs[i].text + '"]').val() === undefined) {
            $('#' + list_id).append($("<option/>").val(add_attrs[i].text).text(add_attrs[i].text));
          }
        }
        sort_list( $("#" + list_id) );
      }
      $("#select-attr-form").modal('toggle');
    });


    /*
     * Modals
     */

    // Winsync-agmt Agreement Wizard

    $("#winsync-create-agmt").on("click", function() {
      clear_winsync_agmt_wizard(); // TODO
    });

    $("#create-agmt").on("click", function() {
      clear_agmt_wizard();
    });

    $("#winsync-agmt-save").on("click", function() {
      var suffix = $("#select-repl-winsync-suffix").val();
      var cmd = [];
      var cmd_args = [];
      var param_err = false;
      var editing = false;
      var init_replica = false;

      // Check passwords match:
      var agmt_passwd = $("#winsync-nsds5replicacredentials").val();
      var passwd_confirm = $("#winsync-nsds5replicacredentials-confirm").val();
      if (agmt_passwd != passwd_confirm) {
        popup_msg("Attention!", "Passwords do not match!");
        return;
      }
      // Get form values
      var repl_root = $("#select-repl-winsync-suffix").val();
      var agmt_name = $("#winsync-agmt-cn").val();
      var win_domain = $("#winsync-nsds7windowsdomain").val();
      var agmt_host = $("#winsync-nsds5replicahost").val();
      var agmt_port = $("#winsync-nsds5replicaport").val();
      var win_subtree = $("#winsync-nsds7windowsreplicasubtree").val();
      var ds_subtree = $("#winsync-nsds7directoryreplicasubtree").val();
      var bind_dn = $("#winsync-nsds5replicabinddn").val();
      var bind_pw = $("#winsync-nsds5replicacredentials").val();
      var agmt_conn = $("#winsync-nsds5replicatransportinfo").val();
      var sync_new_users = "off";
      var sync_new_groups = "off";
      if ( $("#winsync-nsds7newwinusersyncenabled-checkbox").is(":checked") ){
        sync_new_users = "on";
      }
      if ( $("#winsync-nsds7newwingroupsyncenabled-checkbox").is(":checked") ){
        sync_new_groups = "on"
      }
      if ($("#winsync-agmt-wizard-title").text().includes('Edit') ) {
        editing = true;
      }

      // Check required settings
      if (bind_pw == "") {
        $("#winsync-nsds5replicacredentials").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds5replicacredentials").css("border-color", "initial");
        cmd_args.push("--bind-passwd=" + bind_pw);
      }
      if ( ds_subtree == "") {
        $("#winsync-nsds7directoryreplicasubtree").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds7directoryreplicasubtree").css("border-color", "initial");
        cmd_args.push("--ds-subtree=" + ds_subtree);
      }
      if ( win_subtree == "") {
        $("#winsync-nsds7windowsreplicasubtree").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds7windowsreplicasubtree").css("border-color", "initial");
        cmd_args.push("--win-subtree=" + win_subtree);
      }
      if ( win_domain == "") {
        $("#winsync-nsds7windowsdomain").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds7windowsdomain").css("border-color", "initial");
        cmd_args.push("--win-domain=" + win_domain);
      }
      if ( agmt_port == "") {
        $("#winsync-nsds5replicaport").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds5replicaport").css("border-color", "initial");
        cmd_args.push("--port=" + agmt_port);
      }
      if ( agmt_host == "") {
        $("#winsync-nsds5replicahost").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds5replicahost").css("border-color", "initial");
        cmd_args.push('--host=' + agmt_host);
      }
      if ( agmt_conn == "") {
        $("#winsync-nsds5replicatransportinfo").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds5replicatransportinfo").css("border-color", "initial");
        cmd_args.push('--conn-protocol=' + agmt_conn);
      }
      if ( bind_dn == "") {
        $("#winsync-nsds5replicabinddn").css("border-color", "red");
        param_err = true;
      } else {
        $("#winsync-nsds5replicabinddn").css("border-color", "initial");
        cmd_args.push('--bind-dn=' + bind_dn);
      }
      if (param_err ){
        popup_msg("Error", "Missing required parameters");
        return;
      }

      // Checkboxes
      if ( ($("#winsync-nsds7newwinusersyncenabled-checkbox").is(":checked")) ){
        sync_new_users = "on";
      }
      if ( ($("#winsync-nsds7newwingroupsyncenabled-checkbox").is(":checked")) ){
        sync_groups_users = "on";
      }
      if ( ($("#winsync-init-chbx").is(":checked")) ){
        init_replica = true;
      }
      cmd_args.push('--sync-users=' + sync_new_users);
      cmd_args.push('--sync-groups=' + sync_new_groups);

      // Create winsync agreement in DS
      if ( editing ) {
        cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'set', '"' + agmt_name + '"', '--suffix=' + suffix ];
      } else {
        cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'create', '"' + agmt_name + '"', '--suffix=' + suffix];
      }
      cmd = cmd.concat(cmd_args);
      log_cmd('#winsync-agmt-save (click)', 'Create/Set replication winsync agreement', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
        if (editing){
          popup_success('Successfully edited replication winsync agreement');
        } else {
          popup_success('Successfully created replication winsync agreement');
        }
        if (init_replica) {
          // Launch popup stating initialization has begun
          var init_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'init', '--suffix=' + suffix, '"' + agmt_name + '"' ];
          log_cmd('#winsync-agmt-save (click)', 'Initialize winsync agreement', init_cmd);
          cockpit.spawn(init_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
            popup_msg("Agreement Initialization", "The agreement initialization has begun...");
          }).fail(function(data) {
            popup_err("Failed to initialize replication agreement", data.message);
          });
        }
        // Reload table
        get_and_set_repl_winsync_agmts();
      }).fail(function(data) {
        if (editing) {
         popup_err("Failed to edit replication winsync agreement", data.message);
       } else {
         popup_err("Failed to create replication winsync agreement", data.message);
       }
      });

      // Reload winsync agmt table
      $("#winsync-agmt-form").modal('toggle');
    });

    // Create CleanAllRUV Task - TODO
    $("#create-cleanallruv-btn").on("click", function() {
      clear_cleanallruv_form();
    });

    $("#cleanallruv-save").on("click", function() {
      // Do the actual save in DS
      var suffix = $("#cleanallruv-suffix").val();
      var rid = $("#cleanallruv-rid").val();
      var force = false;
      if ( $("#force-clean").is(":checked") ) {
        force = true;
      }
      if (suffix == ""){
        popup_msg("Error", "There is no suffix to run the task on");
        return;
      }
      if (rid == ""){
        popup_msg("Error", "You must enter a Replica ID to clean");
        return;
      }
      $("#cleanallruv-form").modal('toggle');
      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-tasks', 'cleanallruv', '--suffix=' + suffix, '--replica-id=' + rid ];
      if (force) {
        cmd.push('--force-cleaning');
      }
      log_cmd('#cleanallruv-save (click)', 'Create CleanAllRUV Task', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
        let list_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-tasks', 'list-cleanruv-tasks'];
        log_cmd('#cleanallruv-save (click)', 'List all the CleanAllRUV tasks', list_cmd);
        cockpit.spawn(list_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
          repl_clean_table.clear().draw();
          var obj = JSON.parse(data);
          for (var idx in obj['items']) {
            task_attrs = obj['items'][idx]['attrs'];
            var task_create_date = task_attrs['createtimestamp'][0];
            var abort_btn = cleanallruv_action_html;
            if (task_attrs['nstaskstatus'][0].includes('Successfully cleaned rid') ){
              abort_btn = "";
            }
            repl_clean_table.row.add( [
              task_attrs['cn'][0],
              get_date_string(task_create_date),
              suffix,
              task_attrs['replica-id'][0],
              task_attrs['nstaskstatus'][0],
              abort_btn
            ] ).draw( false );
          }
        }).fail( function (data) {
          popup_err("Failed to get CleanAllRUV Tasks", data.message);
        });
      }).fail( function (data) {
        popup_err("Failed to create CleanAllRUV Task", data.message);
      });
    });

    $(document).on('click', '.abort_cleanallruv_btn', function(e) {
      e.preventDefault();
      var data = repl_clean_table.row( $(this).parents('tr') ).data();
      var suffix = $("#cleanallruv-suffix").val();
      var task_rid = data[3];
      popup_confirm("Are you sure you want to abort the cleaning task on: <b>" + suffix + "</b> for Replica ID <b>" + task_rid + "</b>", "Confirmation", function (yes) {
        if (yes) {
          var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-tasks', 'abort-cleanallruv', '--replica-id=' + task_rid, '--suffix=' + suffix];
          log_cmd('.abort_cleanallruv_btn (click)', 'Abort CleanAllRUV task', cmd);
          cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function(data) {
            popup_success("Creating task to abort the CleanAllRUV Task");
          }).fail(function(data) {
            popup_err("Failed to add Abort CleanAllRUV Task", data.message);
          });
        }
      });
    });

    $("#refresh-cleanlist-btn").on('click', function () {
      // Refresh the list
      get_and_set_cleanallruv();
    });
    $("#refresh-agmts-btn").on('click', function () {
      // Refresh the list
      get_and_set_repl_agmts();
    });
    $("#refresh-winsync-agmts-btn").on('click', function () {
      // Refresh the list
      get_and_set_repl_winsync_agmts();
    });

    /*
     * Add repl manager modal
     */
    $("#add-repl-manager").on("click", function() {
      clear_repl_mgr_form();
    });

    $("#add-repl-mgr-save").on("click", function() {
      var suffix = $("#select-repl-cfg-suffix").val();
      var repl_dn = $("#add-repl-mgr-dn").val();
      var repl_pw = $("#add-repl-pw").val();
      var repl_pw_confirm = $("#add-repl-pw-confirm").val();

      // Validate
      if (repl_dn == ""){
        popup_msg("Attention!", "Replication Manager DN is required");
        return;
      }
      if (!valid_dn(repl_dn)){
        popup_msg("Attention!", "Invalid DN for Replication Manager");
        return;
      }
      if (repl_pw == ""){
        popup_msg("Attention!", "Replication Manager DN password is required");
        return;
      }
      if (repl_pw != repl_pw_confirm) {
        popup_msg("Attention!", "Passwords do not match");
        $("#add-repl-pw").val("");
        $("#add-repl-pw-confirm").val("");
        return;
      }

      // Add manager
      var cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','replication', 'create-manager', '--name=' + repl_dn, '--passwd=' + repl_pw, '--suffix=' + suffix ];
      log_cmd('#add-repl-mgr-save (click)', 'Create replication manager entry and add it to config', cmd);
      cockpit.spawn(cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
        get_and_set_repl_config();
        popup_success("Success created replication manager and added it to the replication configuration");
        $("#add-repl-mgr-form").modal('toggle');
      }).fail( function(err) {
        popup_err("Failed to create replication manager entry", err.message);
        $("#add-repl-mgr-form").modal('toggle');
      });
    });

    /* Send update now */
    $(document).on('click', '.agmt-send-updates-btn', function(e) {
      var suffix = $("#select-repl-agmt-suffix").val();
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var update_agmt_name = data[0];

      var update_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'poke', update_agmt_name, '--suffix=' + suffix];
      log_cmd('.agmt-send-updates-btn (click)', 'Trigger send updates now (replication)', update_cmd);
      cockpit.spawn(update_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
        popup_success("Triggered replication updates");
      }).fail( function(err) {
        popup_err("Failed to send updates", err.message);
      });
    });

    /* Send update now (winsync) */
    $(document).on('click', '.winsync-agmt-send-updates-btn', function(e) {
      var suffix = $("#select-repl-winsync-suffix").val();
      var data = repl_winsync_agmt_table.row( $(this).parents('tr') ).data();
      var update_agmt_name = data[0];

      var update_cmd = [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'poke', update_agmt_name, '--suffix=' + suffix];
      log_cmd('.winsync-agmt-send-updates-btn (click)', 'Trigger send updates now (winsync)', update_cmd);
      cockpit.spawn(update_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
        popup_success("Kick started replication");
      }).fail( function(err) {
        popup_err("Failed to send updates", err.message);
      });
    });

    /*
     * Enable/Disable repl agmt
     */
    $(document).on('click', '.agmt-enable-btn', function(e) {
      var suffix = $("#select-repl-agmt-suffix").val();
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var enable_agmt_name = data[0];
      var agmt_state = data[3];  // 4th column in table
      if (agmt_state.toLowerCase() == "enabled") {
        // We must be trying to disable this agreement - confirm it
        popup_confirm("Are you sure you want to disable replication agreement: <b>" + enable_agmt_name + "</b>", "Confirmation", function (yes) {
          if (yes) {
            var disable_cmd =  [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'disable', enable_agmt_name, '--suffix=' + suffix];
            log_cmd('.agmt-enable-btn (click)', 'Disable replication agreement', disable_cmd);
            cockpit.spawn(disable_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
              get_and_set_repl_agmts();
              popup_success("The replication agreement has been disabled.");
            }).fail( function(err) {
              popup_err("Failed to disable agreement", err.message);
            });
          }
        });
      } else {
        // Enabling agreement - confirm it
        popup_confirm("Are you sure you want to enable replication agreement: <b>" + enable_agmt_name + "</b>", "Confirmation", function (yes) {
          if (yes) {
            var enable_cmd =  [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-agmt', 'enable', enable_agmt_name, '--suffix=' + suffix];
            log_cmd('.agmt-enable-btn (click)', 'Enable replication agreement', enable_cmd);
            cockpit.spawn(enable_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
              get_and_set_repl_agmts();
              popup_success("The replication agreement has been enabled.");
            }).fail( function(err) {
              popup_err("Failed to enable agreement", err.message);
            });
          }
        });
      }
    });

    /*
     * Enable/Disable winsync repl agmt
     */
    $(document).on('click', '.winsync-agmt-enable-btn', function(e) {
      var suffix = $("#select-repl-winsync-suffix").val();
      var data = repl_winsync_agmt_table.row( $(this).parents('tr') ).data();
      var enable_agmt_name = data[0];
      var agmt_state = data[3];  // 4th column in table
      if (agmt_state.toLowerCase() == "enabled") {
        // We must be trying to disable this agreement - confirm it
        popup_confirm("Are you sure you want to disable replication agreement: <b>" + enable_agmt_name + "</b>", "Confirmation", function (yes) {
          if (yes) {
            var disable_cmd =  [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'disable', enable_agmt_name, '--suffix=' + suffix];
            log_cmd('.winsync-agmt-enable-btn (click)', 'Disable winsync agreement', disable_cmd);
            cockpit.spawn(disable_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
              get_and_set_repl_winsync_agmts();
              popup_success("The replication agreement has been disabled.");
            }).fail( function(err) {
              popup_err("Failed to disable agreement", err.message);
            });
          }
        });
      } else {
        // Enabling agreement - confirm it
        popup_confirm("Are you sure you want to enable replication agreement: <b>" + enable_agmt_name + "</b>", "Confirmation", function (yes) {
          if (yes) {
            var enable_cmd =  [DSCONF, '-j', 'ldapi://%2fvar%2frun%2f' + server_id + '.socket','repl-winsync-agmt', 'enable', enable_agmt_name, '--suffix=' + suffix];
            log_cmd('.winsync-agmt-enable-btn (click)', 'Enable winsync agreement', disable_cmd);
            cockpit.spawn(enable_cmd, { superuser: true, "err": "message", "environ": [ENV]}).done(function() {
              get_and_set_repl_winsync_agmts();
              popup_success("The replication agreement has been enabled.");
            }).fail( function(err) {
              popup_err("Failed to enable agreement", err.message);
            });
          }
        });
      }
    });

    $("#auth-mgr").click(function() {
      $("#auth-group-div").hide();
      $("#auth-manager-div").show();
    });
    $("#auth-group").click(function() {
      $("#auth-manager-div").hide();
      $("#auth-group-div").show();
    });

    // Page is loaded, mark it as so...
    repl_page_loaded = 1;
  });
});
