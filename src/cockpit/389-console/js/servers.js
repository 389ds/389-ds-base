
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
  $("#server-tasks").hide();
  $("#server-tuning").hide();
  $("#server-tuning").hide();
};

// load the server config pages
$(document).ready( function() {
  $("#server-content").load("servers.html", function () {
     // Initial page setup
    $(".server-cfg-ctrl").hide();
    $("#server-tasks").show();
    $("#server-tasks-btn").focus();

    // Events
    $("#server-config-btn").on("click", function() {
      $(".server-cfg-ctrl").hide();
      $("#server-config").show();
   });
   $("#server-sasl-btn").on("click", function() {
      $(".server-cfg-ctrl").hide();
      $("#server-sasl").show();
    });
    $("#server-pwpolicy-btn").on("click", function() {
     $(".server-cfg-ctrl").hide();
      $("#server-pwpolicy").show();
    });
    $("#server-logs-btn").on("click", function() {
      $(".server-cfg-ctrl").hide();
      $("#server-logs").show();
    });
    $("#server-tasks-btn").on("click", function() {
      $(".server-cfg-ctrl").hide();
      $("#server-tasks").show();
    });
    $("#server-tuning-btn").on("click", function() {
      $(".server-cfg-ctrl").hide();
      $("#server-tuning").show();
    });
    $("#server-ldapi-btn").on("click", function() {
      $(".server-cfg-ctrl").hide();
      $("#server-ldapi").show();
    });

    // Disable disk monitoring input if not in use
    $("#nsslapd-disk-monitoring").change(function() {
      if(this.checked) {
        $('.disk-monitoring').attr('disabled', false);
      } else {
        $('.disk-monitoring').attr('disabled', true);
      }
    });

    $('#sasl-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No SASL Mappings"
      }
    });

    $("#passwordhistory").change(function() {
      if(this.checked) {
        $('#passwordinhistory').attr('disabled', false);
      } else {
        $('#passwordinhistory').attr('disabled', true);
      }
    });

    $("#passwordexp").change(function() {
      if(this.checked) {
        $('#expiration-attrs *').attr('disabled', false);
      } else {
        $('#expiration-attrs *').attr('disabled', true);
      }
    });
    $("#passwordchecksyntax").change(function() {
      if(this.checked) {
        $('#syntax-attrs *').attr('disabled', false);
      } else {
        $('#syntax-attrs *').attr('disabled', true);
      }
    });

    $("#passwordlockout").change(function() {
      if(this.checked) {
        $('#lockout-attrs *').attr('disabled', false);
      } else {
        $('#lockout-attrs *').attr('disabled', true);
      }
    });

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

    $("#nsslapd-ldapilisten").change(function() {
      if(this.checked) {
        $('.ldapi-attrs *').attr('disabled', false);
        if ( $("#nsslapd-ldapiautobind").is(":checked") ){
          $(".autobind-attrs *").attr('disabled', false);
          if ( $("#nsslapd-ldapimaptoentries").is(":checked") ){
            $(".autobind-entry-attrs *").attr('disabled', false);
          } else {
            $(".autobind-entry-attrs *").attr('disabled', true);
          }
        } else {
           $(".autobind-attrs *").attr('disabled', true);
        }
      } else {
        $('.ldapi-attrs *').attr('disabled', true);
      }
    });

    $("#nsslapd-ldapiautobind").change(function() {
      if (this.checked){
        $(".autobind-attrs *").attr('disabled', false);
        if ( $("#nsslapd-ldapimaptoentries").is(":checked") ){
          $(".autobind-entry-attrs *").attr('disabled', false);
        } else {
          $(".autobind-entry-attrs *").attr('disabled', true);
        }
      } else {
        $(".autobind-attrs *").attr('disabled', true);
      }
    });

    $("#nsslapd-ldapimaptoentries").change(function() {
      if (this.checked){
        $(".autobind-entry-attrs *").attr('disabled', false);
      } else {
        $(".autobind-entry-attrs *").attr('disabled', true);
      }
    });

    // Set up agreement table
    $('#passwd-policy-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      //"lengthMenu": [ 16, 32, 64, 128],
      "language": {
        "emptyTable": "No local policies"
      }
    });

    // Accordion opening/closings
    $(".ds-agmt-wiz-panel").css('display','none');
    var acc = document.getElementsByClassName("log-accordion");
    for (var i = 0; i < acc.length; i++) {
      acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

  });
});
