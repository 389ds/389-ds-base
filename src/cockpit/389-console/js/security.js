

// TODO clear form functions


$(document).ready( function() {
  $("#security-content").load("security.html", function () {
    // default setting
    $('#cert-attrs *').attr('disabled', true);

    $(".dropdown").on("change", function() {
      // Refreshes dropdown on Chrome
      $(this).blur();
    });

    $("#sec-config-btn").on("click", function() {
      $(".all-pages").hide();
      $("#security-content").show();
      $("#sec-config").show();
    });

    $("#sec-cacert-btn").on("click", function() {
      $(".all-pages").hide();
      $("#security-content").show();
      $("#sec-cacert-page").show();
    });

    $("#sec-srvcert-btn").on("click", function() {
      $(".all-pages").hide();
      $("#security-content").show();
      $("#sec-svrcert-page").show();
    });
    $("#sec-revoked-btn").on("click", function() {
      $(".all-pages").hide();
      $("#security-content").show();
      $("#sec-revoked-page").show();
    });
    $("#sec-ciphers-btn").on("click", function() {
      $(".all-pages").hide();
      $("#security-content").show();
      $("#sec-ciphers-page").show();
    });

    $("#sec-config").show();

    // Clear forms as theyare clicked

    $("#add-revoked-btn").on('click', function () {
      // TODO Clear form

    });

    $("#add-crl-btn").on('click', function () {
      // Add CRL/CKL

      // Close form
      $("#revoked-form").modal("toggle");
    });

    $('#nssslsupportedciphers').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No ciphers!"
      }
    });

    $("#nsslapd-security").change(function() {
      if(this.checked) {
        $('#cert-attrs *').attr('disabled', false);
      } else {
        $('#cert-attrs *').attr('disabled', true);
      }
    });

    $("#cipher-default-state").change(function() {
      if(this.checked) {
        $("#cipher-table").hide();
      } else {
        $("#cipher-table").show();
      }
    });

    // Set up ca cert table
    $('#ca-cert-table').DataTable( {
      "paging": false,
      "bAutoWidth": false,
      "searching": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No Certificates In Database"
      },
      "columnDefs": [ {
        "targets": 3,
        "orderable": false
      } ]
    });

    // Set up server cert table
    $('#server-cert-table').DataTable( {
      "paging": false,
      "bAutoWidth": false,
      "searching": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No Certificates In Database"
      },
      "columnDefs": [ {
        "targets": 5,
        "orderable": false
      } ]
    });

    // Set up revoked cert table
    $('#revoked-cert-table').DataTable( {
      "paging": false,
      "bAutoWidth": false,
      "searching": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "lengthMenu": [ 10, 25, 50, 100],
      "language": {
        "emptyTable": "No Certificates In Database"
      },
      "columnDefs": [ {
        "targets": 4,
        "orderable": false
      } ]
    });
    // Page is loaded, mark it as so...
    security_page_loaded = 1;
  });
});

