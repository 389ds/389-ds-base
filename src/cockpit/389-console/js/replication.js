var repl_suffix = "";
var prev_repl_role ="no-repl";
var prev_role_button;
var prev_rid = "";
var binddn_list_color = "";

var agmt_action_html = 
  '<div class="dropdown">' +
     '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="dropdownMenu1" data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="dropdownMenu1">' +
      '<li role=""><a role="menuitem" tabindex="0" class="repl-agmt-btn agmt-edit-btn" href="#">View/Edit Agreement</a></li>' +
      '<li role=""><a role="menuitem" tabindex="1" class="repl-agmt-btn" href="#">Initialize Consumer (online)</a></li>' +
      '<li role=""><a role="menuitem" tabindex="-1" class="repl-agmt-btn" href="#">Initialize Consumer (ldif)</a></li>' +
      '<li role=""><a role="menuitem" tabindex="-1" class="repl-agmt-btn" href="#">Send Updates Now</a></li>' +
      '<li role=""><a role="menuitem" tabindex="-1" class="repl-agmt-btn" href="#">Enable/Disable Agreement</a></li>' +
      '<li role=""><a role="menuitem" tabindex="-1" class="repl-agmt-btn agmt-del-btn" href="#">Delete Agreement</a></li>' +
    '</ul>' +
  '</div>';

var winsync_agmt_action_html = 
  '<div class="dropdown">' +
     '<button class="btn btn-default dropdown-toggle ds-agmt-dropdown-button" type="button" id="dropdownMenu2" data-toggle="dropdown">' +
      ' Choose Action...' +
      '<span class="caret"></span>' +
    '</button>' +
    '<ul class="dropdown-menu ds-agmt-dropdown" role="menu" aria-labelledby="dropdownMenu2">' +
      '<li role=""><a role="menuitem" tabindex="0" class="repl-agmt-btn agmt-edit-btn" href="#">View/Edit Agreement</a></li>' +
      '<li role=""><a role="menuitem" tabindex="1" class="repl-agmt-btn" href="#">Send/Receives Updates Now</a></li>' +
      '<li role=""><a role="menuitem" tabindex="-1" class="repl-agmt-btn" href="#">Full Re-synchronization</a></li>' +
      '<li role=""><a role="menuitem" tabindex="-1" class="repl-agmt-btn agmt-del-btn" href="#">Delete Agreement</a></li>' +
    '</ul>' +
  '</div>';

var cleanallruv_action_html =
  '<button class="btn btn-default ds-agmt-dropdown-button" type="button" class="abort-cleanallruv">Abort Task</button>';

function load_repl_jstree() {
  $('#repl-tree').jstree( {
    "plugins" : [ "wholerow" ]
  });
  
  // Set rid for each suffix if applicable
  prev_rid = "1";

  $('#repl-tree').on("changed.jstree", function (e, data) {
    console.log("The selected nodes are:");
    console.log(data.selected);
    repl_suffix = data.selected;
    if (repl_suffix == "repl-changelog-node") {
      $("#repl-changelog").show();
      $("#repl-splash").hide();
      $("#repl-config").hide();
    } else if (repl_suffix == "repl-root") {
      $("#repl-changelog").hide();
      $("#repl-config").hide();
      $("#repl-splash").show();
    } else {
      // Suffix
      $("#repl-splash").hide();
      $("#repl-changelog").hide();
      $("#replica-header").html("Replication Configuration <font size='3'>(<b>" + repl_suffix + "</b>)</font>");
      // Check if this suffix is already setup for replication.  If so set the radio button, and populate the form
      $("#repl-config").show();
    }
  });
};

function clear_agmt_wizard () {
  // Clear input fields and reset dropboxes
  $('.ds-agmt-schedule-checkbox').prop('checked', true);
  $('#agmt-schedule-checkbox').prop('checked', true);
  $(".ds-wiz-input").val("");
  $("#agmt-start-time").val("");
  $("#agmt-end-time").val("");
  $(".ds-agmt-wiz-dropdown").prop('selectedIndex',0);
  $(".ds-accordion-panel").toggle("active");
  $(".ds-accordion-panel").css('display','none');
};

function clear_winsync_agmt_wizard() {
  // Clear out winsync agreement form
  $("#winsync-agmt-cn").val("");
  $("#nsds7windowsdomain").val("");
  $("#winsync-nsds5replicahost").val("");
  $("#winsync-nsds5replicaport").val("");
  $("#nsds7windowsreplicasubtree").val("");
  $("#nsds7directoryreplicasubtree").val("");
  $("#nsds7newwinusersyncenabled-checkbox").prop('checked', false);
  $("#nsds7newwingroupsyncenabled-checkbox").prop('checked', false);
  $("#winsync-nsds5replicabinddn").val("");
  $("#winsync-nsds5replicacredentials").val("");
  $("#winsync-nsds5replicacredentials-confirm").val("");
  $("#winsync-nsds5replicabindmethod").prop('selectedIndex', 0);
}

function clear_cleanallruv_form () {
  // Clear input fields and reset dropboxes
  $('#force-clean').prop('checked', true);
  $("#cleanallruv-rid").val("");
};

function clear_repl_mgr_form () {
  $("#add-repl-pw").val("");
  $("#add-repl-pw-confirm").val("");
  $("#add-repl-mgr-dn").val("");
  $("#add-repl-mgr-checkbox").prop('checked', false);
  $("#add-repl-mgr-passwd").hide();
}

function check_repl_binddn_list () {
  if( $("#nsds5replicabinddngroup").val() == "" && $("#repl-managers-list").has('option').length < 1) {
    $("#repl-managers-list").css('border-color', 'red');
  } else {
    $("#repl-managers-list").css('border-color', binddn_list_color);
  }
}

$(document).ready( function() {
  $("#repl-backend-selection").load("replication.html", function () {
    // Load the tree
    load_repl_jstree();
    $('#tree').on("changed.jstree", function (e, data) {
      console.log("The selected nodes are:");
      console.log(data.selected);
      var suffix = data.selected;
    });
    binddn_list_color = $("#repl-managers-list").css("border-color");

    // Load existing replication config (if any), set role, etc

    // Check repl managers list and if empty give it a red border
    check_repl_binddn_list();

    $("#set-default").on("click", function() {
      $("#nsslapd-changelogdir").val("/var/lib/dirsrv/" + server_id + "/changelogdb");
    });
    
    $("#nsds5replicaid").on("change", function() {
      prev_rid = $("#nsds5replicaid").val();
    });

    $("#save-repl-cfg-btn").on('click', function () {
       // validate values

       // Do the save in DS 

       // In case we updated the bind group...
       check_repl_binddn_list();
    });

    /* 
     * Setting/Changing the replication role 
     */
    $("#change-repl-role").on("click", function() {
      var role_button = $("input[name=repl-role]:checked");
      var role = $("input[name=repl-role]:checked").val();

      if (prev_repl_role == role) {
        // Nothing changed
        return;
      }

      if (role == "master") {
        if ($("#nsds5replicaid").val() == "" || $("#nsds5replicaid").val() === undefined ){
          alert("Replica ID is required for a Master role");
          return;
        }
        if ( !valid_num($("#nsds5replicaid").val()) ) {
          alert("Replica ID must be a number");
          return;
        }

        // TODO - check if replication is set up, if not launch a basic popup windows asking for replica ID

        // TODO - if prev_role is not "disabled" after for confirmation before promoting replica
        $("#repl-settings-header").html("Master Replication Settings");
        $('#repl-cleanallruv').show();
        $('#repl-agmts').show();
        $("#nsds5replicaid").prop('disabled', true); // Can not edit rid after setup
      } else {
        $("#nsds5replicaid").prop('disabled', false);
        $('#repl-cleanallruv').hide()
        $("#nsds5replicaid").prop('required', false);

        if (role == "hub"){
          // TODO - if prev_role is not "disabled" after for confirmation before promoting/demoting replica
          $("#nsds5replicaid").val("");
          $("#repl-settings-header").html("Hub Replication Settings");
          $('#repl-agmts').show();
        } else if (role == "consumer") {
          // consumer
          // TODO - if prev_role is not "disabled" after for confirmation before demoting replica
          $('#repl-agmts').hide();
          $("#nsds5replicaid").val("");
          $("#repl-settings-header").html("Consumer Replication Settings");   
        }
      }
      if (role == "no-repl") {
        // This also means disable replication: delete agmts, everything
        if (confirm("Are you sure you want to disable replication and remove all agreements?")){
          //delete everything
          $("#repl-form").hide();
          $("#nsds5replicaid").val("");
        } else {
          prev_role_button.prop('checked', true);
          return;
        }
      } else {
        $("#repl-form").show();
      }
      prev_role_button = role_button;
      prev_repl_role = role;
    });

    // Set up agreement table
    var repl_agmt_table = $('#repl-agmt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      //"lengthMenu": [ 16, 32, 64, 128],
      "language": {
        "emptyTable": "No agreements configured"
      },
      "columnDefs": [ {
        "targets": 5,
        "orderable": false
      } ]
    });

    // Set up windows sync agreement table
    var repl_winsync_agmt_table = $('#repl-winsync-agmt-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      //"lengthMenu": [ 16, 32, 64, 128],
      "language": {
        "emptyTable": "No winsync agreements configured"
      },
      "columnDefs": [ {
        "targets": 6,
        "orderable": false
      } ]
    });

    // Set up CleanAllRUV Table
    var repl_clean_table = $('#repl-clean-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No agreements configured"
      },
      "columnDefs": [ {
        "targets": 3,
        "orderable": false
      } ]
    });

    $('#repl-summary-table').DataTable( {
      "paging": false,
      "searching": false,
      "bInfo" : false,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No Replicated Suffixes"
      }
    });

    // Repl Agreement Wizard
    $("#agmt-close").on("click", function() {
      $("#agmt-form").css('display', 'none');
    });
    $("#agmt-cancel").on("click", function() {
      $("#agmt-form").css('display', 'none');
    });
    $("#create-agmt").on("click", function() {
      clear_agmt_wizard();
      $("#agmt-form").css('display', 'block');
    });
    $("#agmt-save").on("click", function() {
      // Get all the settings
      var agmt_name = $("#agmt-cn").val();
      var agmt_host = $("#nsds5replicahost").val();
      var agmt_port = $("#nsds5replicaport").val();
      var agmt_bind = $("#nsds5replicabinddn").val();
      var agmt_bindpw = $("#nsds5replicacredentials").val();
      var agmt_bindpw_confirm = $("#nsds5replicacredentials-confirm").val();
      var agmt_conn = $("#nsds5replicatransportinfo").val();
      var agmt_method = $("#nsds5replicabindmethod").val();
      var agmt_exclude = $("#frac-list").val();  // exclude list
      var agmt_tot_exclude = $("#frac-total-list").val();  // total init exclude list
      var agmt_strip = $("#frac-strip-list").val();
      var agmt_schedule = "";
      var agmt_init = $("#init_options").val();

      // Confirm passwords match
      if (agmt_bindpw != agmt_bindpw_confirm) {
        alert("Passwords do not match");
        return;
      }

      if ( !$("#agmt-schedule-checkbox").is(":checked") ){
        agmt_start = $("#agmt-start-time").val().replace(':','');
        agmt_end = $("#agmt-end-time").val().replace(':','');

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
        agmt_schedule = agmt_start + "-" + agmt_end + " " + agmt_days
      }

      // TODO add agmt to DS

      // TODO Do agmt init (if requested)

      // TODO get agmt status for table
      var agmt_status = "";

      // Update Agmt Table
      repl_agmt_table.row.add( [
            agmt_name,
            agmt_host,
            agmt_port,
            "Enabled",
            agmt_status,
            agmt_action_html
        ] ).draw( false );

      // Done, close the form
      $("#agmt-form").css('display', 'none');
      clear_agmt_wizard();
    });

    // Delete agreement
    $(document).on('click', '.agmt-del-btn', function(e) {
      e.preventDefault();
      // TODO  -delete agreement in DS

      // Update HTML table
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var del_agmt_name = data[0];
      if ( confirm("Are you sure you want to delete replication agreement: " + del_agmt_name) ) {
        // TODO Delete schema
          // Update html table
        repl_agmt_table.row( $(this).parents('tr') ).remove().draw( false );
      }
    });

    // Edit Agreement
    $(document).on('click', '.agmt-edit-btn', function(e) {
      e.preventDefault();
      clear_agmt_wizard();

      // Update HTML table
      var data = repl_agmt_table.row( $(this).parents('tr') ).data();
      var edit_agmt_name = data[0];

      // TODO Get agreement from DS

      // Set agreement form values
      $("#agmt-wizard-title").html("<b>Edit Replication Agreement</b>");

      // Open form
      $("#agmt-form").css('display', 'block');
    });

    // Handle disabling/enabling of agmt schedule panel
    $('#agmt-schedule-panel *').attr('disabled', true); /// Disabled by default
    $("#agmt-schedule-checkbox").change(function() {
      if(this.checked) {
        $('#agmt-schedule-panel *').attr('disabled', true);
      } else {
        $('#agmt-schedule-panel *').attr('disabled', false);
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

    // Create time ticket for agmt schedule (start end times))
    $('input.timepicker').timepicker({
      'timeFormat': 'H:i',
      'disableTextInput': true
    });

    // Accordion opening/closings
    $(".ds-accordion-panel").css('display','none');
    var acc = document.getElementsByClassName("repl-accordion");
    for (var i = 0; i < acc.length; i++) {
      acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        /*if (panel.style.maxHeight && panel.style.maxHeight != "0px"){
          panel.style.maxHeight = null;
        } else {
          panel.style.maxHeight = panel.scrollHeight + "px";
        }*/
        if (panel.style.display == "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var repl_acc = document.getElementsByClassName("repl-config-accordion");
    for (var i = 0; i < repl_acc.length; i++) {
      repl_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display == "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }
    var repl_agmt_acc = document.getElementsByClassName("repl-agmt-accordion");
    for (var i = 0; i < repl_agmt_acc.length; i++) {
      repl_agmt_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display == "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var repl_winsync_agmt_acc = document.getElementsByClassName("repl-winsync-agmt-accordion");
    for (var i = 0; i < repl_winsync_agmt_acc.length; i++) {
      repl_winsync_agmt_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display == "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }


    var repl_cleanruv_acc = document.getElementsByClassName("repl-cleanruv-accordion");
    for (var i = 0; i < repl_cleanruv_acc.length; i++) {
      repl_cleanruv_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display == "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    /*
     * Handle the repl agmt wizard select lists
     */

    // Fractional attrs
    $("#frac-list-add-btn").on("click", function () {
      var add_attrs = $("#frac-attr-list").val();
      if (add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#frac-list option[value="' + add_attrs[i] + '"]').val() === undefined) {
            $('#frac-list').append($("<option/>") .val(add_attrs[i]) .text(add_attrs[i]));
          }
        }
        $("#frac-attr-list").find('option:selected').remove();
      }
    });
    $("#frac-list-remove-btn").on("click", function () {
      var add_attrs = $("#frac-list").find('option:selected');
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#frac-attr-list option[value="' + add_attrs[i].text + '"]').val() === undefined) {
            $('#frac-attr-list').append($("<option/>").val(add_attrs[i].text).text(add_attrs[i].text));
          }
        }
      }
      $("#frac-list").find('option:selected').remove();
      sort_list( $("#frac-attr-list") );
    });


    // Total Fractional attrs
    $("#frac-total-list-add-btn").on("click", function () {
      var add_attrs = $("#total-attr-list").val();
      if (add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#frac-total-list option[value="' + add_attrs[i] + '"]').val() === undefined) {
            // Not a duplicate
            $('#frac-total-list').append($("<option/>") .val(add_attrs[i]) .text(add_attrs[i]));
          }
        }
        $("#total-attr-list").find('option:selected').remove();
      }
    });
    $("#frac-total-list-remove-btn").on("click", function () {
      var add_attrs = $("#frac-total-list").find('option:selected');
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#total-attr-list option[value="' + add_attrs[i].text + '"]').val() === undefined) {
            $('#total-attr-list').append($("<option/>").val(add_attrs[i].text).text(add_attrs[i].text));
          }
        }
      }
      $("#frac-total-list").find('option:selected').remove();
      sort_list( $("#total-attr-list") );
    });

    // Strip Fractional attrs
    $("#frac-strip-list-add-btn").on("click", function () {
      var add_attrs = $("#strip-attr-list").val();
      if (add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#frac-strip-list option[value="' + add_attrs[i] + '"]').val() === undefined) {
            // Not a duplicate
            $('#frac-strip-list').append($("<option/>") .val(add_attrs[i]) .text(add_attrs[i]));
          }
        }
        $("#strip-attr-list").find('option:selected').remove();
      }
    });
    $("#frac-strip-list-remove-btn").on("click", function () {
      var add_attrs = $("#frac-strip-list").find('option:selected');
      if (add_attrs && add_attrs != '' && add_attrs.length > 0) {
        for (var i = 0; i < add_attrs.length; i++) {
          if ( $('#strip-attr-list option[value="' + add_attrs[i].text + '"]').val() === undefined) {
            $('#strip-attr-list').append($("<option/>").val(add_attrs[i].text).text(add_attrs[i].text));
          }
        }
      }
      $("#frac-strip-list").find('option:selected').remove();
      sort_list( $("#strip-attr-list") );
    });

    // Modals




    // Winsync-agmt Agreement Wizard
    $("#winsync-agmt-close").on("click", function() {
      $("#winsync-agmt-form").css('display', 'none');
    });
    $("#winsync-agmt-cancel").on("click", function() {
      $("#winsync-agmt-form").css('display', 'none');
    });
    $("#winsync-create-agmt").on("click", function() {
      clear_winsync_agmt_wizard(); // TODO
      $("#winsync-agmt-form").css('display', 'block');
    });
    $("#winsync-agmt-save").on("click", function() {

      // Check passwords match:

      var agmt_passwd = $("#winsync-nsds5replicacredentials").val();
      var passwd_confirm = $("#winsync-nsds5replicacredentials-confirm").val();

      if (agmt_passwd != passwd_confirm) {
        alert("Passwords do not match!");
        return;
      }
      // Get form values
      var repl_root = repl_suffix;
      var agmt_name = $("#winsync-agmt-cn").val();
      var win_domain = $("#nsds7windowsdomain").val();
      var agmt_host = $("#winsync-nsds5replicahost").val();
      var agmt_port = $("#winsync-nsds5replicaport").val();
      var win_subtree = $("#nsds7windowsreplicasubtree").val();
      var ds_subtree = $("#nsds7directoryreplicasubtree").val();
      var bind_dn = $("#winsync-nsds5replicabinddn").val();
      var conn_protocol = $("#winsync-nsds5replicabindmethod").val();
      var sync_new_users = "no";
      var sync_new_groups = "no";
      if ( $("#nsds7newwinusersyncenabled-checkbox").is(":checked") ){
        sync_new_users = "yes";
      }
      if ( $("#nsds7newwingroupsyncenabled-checkbox").is(":checked") ){
        sync_new_groups = "yes"
      }

      // Validate


      // Update DS

      // Get status
      var agmt_status = "";

      // Update Winsync Agmt Table
      repl_winsync_agmt_table.row.add( [
        agmt_name,
        agmt_host,
        agmt_port,
        ds_subtree,
        win_subtree,
        agmt_status,
        winsync_agmt_action_html
      ] ).draw( false );

      // Done
      $("#winsync-agmt-form").css('display', 'none');
      clear_winsync_agmt_wizard();
    });

    // Create CleanAllRUV Task
    $("#create-cleanallruv-btn").on("click", function() {
      clear_cleanallruv_form();
      $("#cleanallruv-form").css('display', 'block');
    });
    $("#cleanallruv-close").on("click", function() {
      $("#cleanallruv-form").css('display', 'none');
    });
    $("#cleanallruv-cancel").on("click", function() {
      $("#cleanallruv-form").css('display', 'none');
    });
    $("#cleanallruv-save").on("click", function() {
      $("#cleanallruv-form").css('display', 'none');
      // Do the actual save in DS
      // Update html

      // Update Agmt Table
      repl_clean_table.row.add( [
        "Creation date WIP",
        $("#cleanallruv-rid").val(),
        "Task starting...",
        cleanallruv_action_html
      ] ).draw( false );
    });

    // Add repl manager
    $("#add-repl-mgr-checkbox").change(function() {
      if(this.checked) {
        $("#add-repl-mgr-passwd").show();
      } else {
        $("#add-repl-mgr-passwd").hide();
        $("#add-repl-pw").val("");
        $("#add-repl-pw-confirm").val("");
      }
    });

    // Remove repl manager
    $("#delete-repl-manager").on("click", function () {
      var repl_mgr_dn = $("#repl-managers-list").find('option:selected');
      if (repl_mgr_dn.val() !== undefined) {
        if ( confirm("Are you sure you want to delete replication manager: " + repl_mgr_dn.val() ) ) {
          // TODO Update replica config entry, do not delete the real repl mgr entry

          // Update HTML
          repl_mgr_dn.remove();
          check_repl_binddn_list();
        }
      }
    });

    // Add repl manager modal
    $("#add-repl-manager").on("click", function() {
      clear_repl_mgr_form();
      $("#add-repl-mgr-form").css('display', 'block');
    });
    $("#add-repl-mgr-close").on("click", function() {
      $("#add-repl-mgr-form").css('display', 'none');
    });
    $("#add-repl-mgr-cancel").on("click", function() {
      $("#add-repl-mgr-form").css('display', 'none');
    });
    $("#add-repl-mgr-save").on("click", function() {
      var repl_dn = $("#add-repl-mgr-dn").val();
      if (repl_dn == ""){
        alert("Replication Manager DN is required");
        return;
      }
      if ( $("#add-repl-mgr-checkbox").is(":checked") ){
        // Confirm passwords match
        var agmt_bindpw = $("#add-repl-pw").val();
        var agmt_bindpw_confirm = $("#add-repl-pw-confirm").val();
        if (agmt_bindpw != agmt_bindpw_confirm) {
          alert("Passwords do not match");
          $("#add-repl-pw").val("");
          $("#add-repl-pw-confirm").val("");
          return;
        }
      }

      console.log("Validate dn...");
      if (!valid_dn(repl_dn)){
        alert("Invalid DN for Replication Manager");
        return;
      }
 

      // Do the actual save in DS

      // Update html
      $("#add-repl-mgr-form").css('display', 'none');
      if (repl_dn != '') {
        if ( $('#repl-managers-list option[value="' + repl_dn + '"]').val() === undefined) {
          // It's not a duplicate
          $('#repl-managers-list').append($("<option/>") .val(repl_dn).text(repl_dn));
          check_repl_binddn_list();
        }
      }
    });


    $(document).on('click', '.abort-cleanallruv-btn', function(e) {
       // TODO - abort the cleantask - update table (remove or update existing clean task?)
    });


  });
});
