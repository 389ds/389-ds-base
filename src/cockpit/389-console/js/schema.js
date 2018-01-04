
$(document).ready( function() {
  $("#schema-page").load("schema.html", function (){

    // Setup the tables: standard, custom, and Matching Rules
    $('#standard-oc-table').DataTable({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
      "language": {
        "emptyTable": "No objectclasses available"
      },
      "lengthMenu": [[25, 50, 100, -1], [25, 50, 100, "All"]]
    });

    $('#standard-attr-table').DataTable( {
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
      "language": {
        "emptyTable": "No attributes available"
      },
      "lengthMenu": [[25, 50, 100, -1], [25, 50, 100, "All"]]
    });

    $('#custom-oc-table').DataTable ({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No custom objectclasses defined"
      },
      "lengthMenu": [[25, 50, 100, -1], [25, 50, 100, "All"]]
    });
    $('#custom-attr-table').DataTable({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip', // Moves the search box to the left
      "language": {
        "emptyTable": "No custom attributes defined"
      },
      "lengthMenu": [[25, 50, 100, -1], [25, 50, 100, "All"]]
    });

    $('#schema-mr-table').DataTable({
      "paging": true,
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No matching rules defined"
      },
      "lengthMenu": [[25, 50, 100, -1], [25, 50, 100, "All"]]
    });

    // Set the initial startup panel
    $(".schema-ctrl").hide();
    $("#schema-standard").show();

    $("#schema-standard-btn").on("click", function() {
      $(".schema-ctrl").hide();
      $("#schema-standard").show();
   });

    $("#schema-custom-btn").on("click", function() {
      $(".schema-ctrl").hide();
      $("#schema-custom").show();
   });
   $("#schema-mr-btn").on("click", function() {
      $(".schema-ctrl").hide();
      $("#schema-mr").show();
   });

  });
});

/*
$('.ds-buttons').each(function(){
  var $this = $(this);
  var $button = $this.find('button.active');

  $this.on('click', '.ds-button-control', function(e) {
    var id = this.hash;

    if (id && !$button.is('.active')) {
      $button.removeClass('active');
      $button = $(id).addClass('active');
    }
  });
});
*/
