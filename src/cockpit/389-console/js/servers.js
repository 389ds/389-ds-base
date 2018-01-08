
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
  // Fill in the server instance dropdown  
  get_insts();

  $("#server-content").load("servers.html", function () {
    // Initial page setup
    $(".server-cfg-ctrl").hide();
    $("#server-tasks").show();
    $("#server-tasks-btn").focus().select();
    
    // To remove text border on firefox on dropdowns)
    if(navigator.userAgent.toLowerCase().indexOf('firefox') > -1) {  
      $("select").focus( function() {      
        this.style.setProperty( 'outline', 'none', 'important' );
        this.style.setProperty( 'color', 'rgba(0,0,0,0)', 'important' );
        this.style.setProperty( 'text-shadow', '0 0 0 #000', 'important' );
      });
    }
    
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
    $(".ds-accordion-panel").css('display','none');
    var log_acc = document.getElementsByClassName("log-accordion");
    for (var i = 0; i < log_acc.length; i++) {
      log_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var cfg_acc = document.getElementsByClassName("config-accordion");
    for (var i = 0; i < cfg_acc.length; i++) {
      cfg_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var rootdn_acc = document.getElementsByClassName("rootdn-accordion");
    for (var i = 0; i < rootdn_acc.length; i++) {
      rootdn_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var localpwp_acc = document.getElementsByClassName("localpwp-accordion");
    for (var i = 0; i < localpwp_acc.length; i++) {
      localpwp_acc[i].onclick = function() {
        this.classList.toggle("active");
        var panel = this.nextElementSibling;
        if (panel.style.display === "block") {
            panel.style.display = "none";
        } else {
            panel.style.display = "block";
        }
      }
    }

    var advcfg_acc = document.getElementsByClassName("adv-config-accordion");
    for (var i = 0; i < advcfg_acc.length; i++) {
      advcfg_acc[i].onclick = function() {
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
