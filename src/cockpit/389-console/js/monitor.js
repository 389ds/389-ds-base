var accesslog_cont_refresh;
var auditlog_cont_refresh;
var auditfaillog_cont_refresh;
var erorrslog_cont_refresh;

function gen_ratio_chart(ratio, chart ) {
  var c3ChartDefaults = patternfly.c3ChartDefaults();
  var donutConfig = c3ChartDefaults.getDefaultDonutConfig(ratio + "%");
  var miss = 100 - ratio;
  var donut_color = patternfly.pfPaletteColors.lightGreen;
  if (ratio < 90) {
    donut_color = patternfly.pfPaletteColors.red;
  }
  donutConfig.bindto = chart;
  donutConfig.data = {
    type: "donut",
    columns: [
      ["Hit Ratio", ratio],
      ["Miss", miss],
    ],
    colors: {
      'Hit Ratio': donut_color,
      'Miss': "#D8D8D8"
    },
    order: null
  };
  donutConfig.size = {
    width: 120,
    height: 80
  };

  c3.generate(donutConfig);
};

function gen_util_chart(used, maxsize, hitratio, chart ) {
  var c3ChartDefaults = patternfly.c3ChartDefaults();
  var ratio = Math.round((used / maxsize) * 100);
  var donutConfig = c3ChartDefaults.getDefaultDonutConfig(ratio + "%");
  var avail = maxsize - used;
  donutConfig.bindto = chart;
  var donut_color = patternfly.pfPaletteColors.lightGreen;
  if (hitratio < 90 && ratio > 90) {
    donut_color = patternfly.pfPaletteColors.red;
  }

  donutConfig.data = {
    type: "donut",
    columns: [
      ["Used", used],
      ["Available", avail],
    ],
    colors: {
      'Used': donut_color,
      'Available': "#D8D8D8"
    },
    order: null
  };
  donutConfig.size = {
    width: 120,
    height: 80
  };
  c3.generate(donutConfig);
};

/*
 *  Refresh logs
 */
function refresh_access_log () {
  var access_log = "/var/log/dirsrv/" + server_id + "/access";  // TODO - get actual log location from config
  var lines = $("#accesslog-lines").val();
  var logging = cockpit.spawn(["tail", "-" + lines, access_log], 
                              { "superuser": "try" }).done(function(data) {
    $("#accesslog-area").text(data); 
  });
}

function refresh_audit_log () {
  var audit_log = "/var/log/dirsrv/" + server_id + "/audit";  // TODO - get actual log location from config
  var lines = $("#auditlog-lines").val();
  var logging = cockpit.spawn(["tail", "-" + lines, audit_log], 
                              { "superuser": "try" }).done(function(data) {
    $("#auditlog-area").text(data); 
  });
}

function refresh_auditfail_log () {
  var auditfail_log = "/var/log/dirsrv/" + server_id + "/auditfail";  // TODO - get actual log location from config
  var lines = $("#auditfaillog-lines").val();
  var logging = cockpit.spawn(["tail", "-" + lines, auditfail_log], 
                              { "superuser": "try" }).done(function(data) {
    $("#auditfaillog-area").text(data); 
  });
}

function refresh_errors_log () {
  var errors_log = "/var/log/dirsrv/" + server_id + "/errors";  // TODO - get actual log location from config
  var lines = $("#errorslog-lines").val();
  var logging = cockpit.spawn(["tail", "-" + lines, errors_log], 
                              { "superuser": "try" }).done(function(data) {
    $("#errorslog-area").text(data);
    
  });
}


$(document).ready( function() {
  $("#monitor-content").load("monitor.html", function () {

    $('#monitor-db-tree').jstree({
      "plugins": [ "contextmenu", "wholerow" ],
    });

    $("#monitor-server-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      $("#monitor-server-page").show();
    });

    $("#monitor-db-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      
      // TODO - NDN cache prior to 1.4.0 is duplicated under each suffix/backend monitor - 
      // so we need to bring it forward to the global database stats here
      var db_hitratio = '99';
      var ndn_hitratio = '83';
      var ndn_maxsize = '25165824';
      var ndn_cursize = '19891200';
      gen_ratio_chart(db_hitratio, '#monitor-db-cache-hitratio-chart');
      gen_ratio_chart(ndn_hitratio, '#monitor-ndn-cache-hitratio-chart');
      gen_util_chart(ndn_cursize, ndn_maxsize, ndn_hitratio, '#monitor-ndn-cache-util-chart');

      $("#monitor-db-page").show();
    });


    $("#monitor-snmp-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      $("#monitor-snmp-page").show();
    });

    $("#monitor-repl-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      $("#monitor-repl-page").show();
    });

    $("#monitor-log-access-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      refresh_access_log();
      $("#monitor-log-access-page").show();
    });
    $("#monitor-log-audit-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      refresh_audit_log();
      $("#monitor-log-audit-page").show();
    });
    $("#monitor-log-auditfail-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      refresh_auditfail_log()
      $("#monitor-log-auditfail-page").show();
    });
    $("#monitor-log-errors-btn").on("click", function() {
      $(".all-pages").hide();
      $("#monitor-content").show();
      refresh_errors_log();
      $("#monitor-log-errors-page").show();
    });


    $("#accesslog-refresh-btn").on('click', function () {
      refresh_access_log();
    });
    $("#auditlog-refresh-btn").on('click', function () {
      refresh_audit_log();
    });
    $("#auditfaillog-refresh-btn").on('click', function () {
      refresh_auditfail_log();
    });
    $("#errorslog-refresh-btn").on('click', function () {
      refresh_errors_log();
    });

    $('#monitor-db-tree').on("changed.jstree", function (e, data) {
      console.log("MONITOR The selected nodes are:");
      console.log(data.selected);
      var tree_node = data.selected;
      if (tree_node == "monitor-db-main") {

        // TODO - NDN cache prior to 1.4.0 is duplicated under each suffix/backend monitor - 
        // so we need to bring it forward to the global database stats here
        var db_hitratio = '99';
        var ndn_hitratio = '83';
        var ndn_maxsize = '25165824';
        var ndn_cursize = '19891200';

        gen_ratio_chart(db_hitratio, '#monitor-db-cache-hitratio-chart');
        gen_ratio_chart(ndn_hitratio, '#monitor-ndn-cache-hitratio-chart');
        gen_util_chart(ndn_cursize, ndn_maxsize, ndn_hitratio, '#monitor-ndn-cache-util-chart');
        $("#monitor-suffix-page").hide();
        $("#db-content").show();
      } else if (tree_node[0].startsWith("monitor-suffix-")) {
        /*
         * Gather and set the Suffix info
         */
        var monitor_suffix = tree_node[0].replace("monitor-suffix-", "");
        $("#monitor-suffix-header").html("<b>" + monitor_suffix + "</b>");

        // TODO - get the monitor info.  For now uses DEMO values for the charts
        var entry_hitratio = '96';
        var entry_maxsize = '512000';
        var entry_cursize = '395000';
        var dn_hitratio = '89';
        var dn_maxsize = '51200';
        var dn_cursize = '51200';
        
        // Generate the donut charts
        gen_ratio_chart(entry_hitratio, '#monitor-entry-cache-hitratio-chart');
        gen_util_chart(entry_cursize, entry_maxsize, entry_hitratio, '#monitor-entry-cache-util-chart');
        gen_ratio_chart(dn_hitratio, '#monitor-dn-cache-hitratio-chart');
        gen_util_chart(dn_cursize, dn_maxsize, dn_hitratio, '#monitor-dn-cache-util-chart');
        $("#db-content").hide();
        $("#monitor-suffix-page").show();
      }
    });

    var monitor_conn_table = $('#monitor-conn-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "searching": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No active connections",
        "search": "Search Connections"
      }
    });
    var monitor_index_table = $('#monitor-index-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No Attribute Indexes",
        "search": "Search Indexes"
      }
    });

    $.fn.dataTable.moment( 'HH:mm:ss' );
    var monitor_repl_table = $('#monitor-repl-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No Replication Agreements",
        "search": "Search Agreements"
      }
    });

    var monitor_winsync_table = $('#monitor-winsync-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No Winsync Agreements",
        "search": "Search Agreements"
      }
    });

    // The continuous log refresh intervals
    $("#accesslog-cont-refresh").change(function() {
      if(this.checked) {
        accesslog_cont_refresh = setInterval(refresh_access_log, 1000);
      } else {
        clearInterval(accesslog_cont_refresh);
      }
    });

    $("#auditlog-cont-refresh").change(function() {
      if(this.checked) {
        auditlog_cont_refresh = setInterval(refresh_audit_log, 1000);
      } else {
        clearInterval(auditlog_cont_refresh);
      }
    });

    $("#auditfaillog-cont-refresh").change(function() {
      if(this.checked) {
        auditfaillog_cont_refresh = setInterval(refresh_auditfail_log, 1000);
      } else {
        clearInterval(auditfaillog_cont_refresh);
      }
    });

    $("#errorslog-cont-refresh").change(function() {
      if(this.checked) {
        errorslog_cont_refresh = setInterval(refresh_errors_log, 1000);
      } else {
        clearInterval(errorslog_cont_refresh);
      }
    });

    $(document).on('click', '.repl-detail-btn', function(e) {
      e.preventDefault();
      var data = monitor_repl_table.row( $(this).parents('tr') ).data();
      var agmt_name = data[0];
      var agmt_suffix = data[2];
      var agmt_enabled = "off";  // TODO Need to determine this from DS data
      var agmt_status = "";
      if (agmt_enabled == "off") {
          agmt_status = "&nbsp;<font size=\"2\" color=\"red\"><b>(Agreement Disabled)</b></font>";
      } 
      // clear_agmt_form();

      $("#monitor-agmt-header").html("<b>Replication Agreement Details:</b>&nbsp;&nbsp; " + agmt_name + " " + agmt_status);

      // TODO  - get agreement details and populate form
      $("#monitor-agmt-form").css('display', 'block');
    });


    $(document).on('click', '.repl-winsync-detail-btn', function(e) {
      e.preventDefault();
      var data = monitor_repl_table.row( $(this).parents('tr') ).data();
      var agmt_name = data[0];
      var agmt_suffix = data[2];
      var agmt_enabled = "on";  // TODO Need to determine this from DS data
      var agmt_status = "";
      if (agmt_enabled == "off") {
          agmt_status = "&nbsp;<font size=\"2\" color=\"red\"><b>(Agreement Disabled)</b></font>";
      } 
      // clear_agmt_form();

      $("#repl-winsync-agmt-header").html("<b>Winsync Agreement Details:</b>&nbsp;&nbsp; " + agmt_name + " " + agmt_status);
      console.log("MARK we are here");
      // TODO  - get agreement details and populate form
      $("#monitor-winsync-agmt-form").css('display', 'block');
    });

    
  });
});
