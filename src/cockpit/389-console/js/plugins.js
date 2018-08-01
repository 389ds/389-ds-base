$(document).ready( function() {
  $("#plugin-content").load("plugins.html", function () {
    $('#plugin-table').DataTable ( {
      "lengthMenu": [[10, 25, 50, -1], [10, 25, 50, "All"]],
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "language": {
        "emptyTable": "No plugins",
        "search": "Search Plugins"
      },
      "columnDefs": [ {
        "targets": 3,
        "orderable": false
      } ]
    });

    $("#plugin-tab").on("click", function() {
      $(".all-pages").hide();
      $("#plugin-content").show();
    });
    // Page is loaded, mark it as so...
    plugin_page_loaded = 1;
  });
});
