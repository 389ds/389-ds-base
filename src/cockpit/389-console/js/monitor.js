
function monitor_hide_all(){
  //  Better way to do this with jquery & name||roles
  $("#monitor-server").hide();
  $("#monitor-db").hide();
  $("#monitor-repl").hide();
  $("#monitor-db-state").hide();
  $("#monitor-logs").hide();
  $("#monitor-snmp").hide();
};

$(document).ready( function() {
  $("#monitor-content").load("monitor.html", function () {
    monitor_hide_all();
    $("#monitor-server").show();
    $("#monitor-server-btn").addClass('active');

    $("#monitor-server-btn").on("click", function() {
      monitor_hide_all();
      $("#monitor-server").show();
   });
    $("#monitor-db-btn").on("click", function() {
      monitor_hide_all();
      $("#monitor-db").show();
   });
   $("#monitor-repl-btn").on("click", function() {
     monitor_hide_all();
     $("#monitor-repl").show();
   });
   $("#monitor-db-state-btn").on("click", function() {
     monitor_hide_all();
     $("#monitor-db-state").show();
   });
   $("#monitor-logs-btn").on("click", function() {
     monitor_hide_all();
     $("#monitor-logs").show();
   });
   $("#monitor-snmp-btn").on("click", function() {
     monitor_hide_all();
     $("#monitor-snmp").show();
   });
  });
});
