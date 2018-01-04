
$(document).ready( function() {
  $("#security-selection").load("security.html", function () {
    // default setting
    $('#cert-attrs *').attr('disabled', true);
    
    $("#sec-config").show();
     $('#nsSSLSupportedCiphers').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No agreements configured"
      }
    });
    $('#nsSSLEnabledCiphers').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No agreements configured"
      }
    });
    
    // TODO: Get config settings and populate tables, forms, and set check boxes, etc

    $("#sec-config-btn").on("click", function() {
     $(".security-ctrl").hide();
      $("#sec-config").show();
    });
    $("#sec-cert-btn").on("click", function() {
     $(".security-ctrl").hide();
      $("#sec-certs").show();
    });
    $("#sec-cipher-btn").on("click", function() {
     $(".security-ctrl").hide();
      $("#sec-ciphers").show();
    });
    
    $("#nsslapd-security").change(function() {
      if(this.checked) {
        $('#cert-attrs *').attr('disabled', false);
      } else {
        $('#cert-attrs *').attr('disabled', true);
      }
    });

    // Set up agreement table
    $('#cert-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No Certificates In Database"
      },
    });


  });
});

