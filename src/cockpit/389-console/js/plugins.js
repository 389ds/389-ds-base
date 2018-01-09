$(document).ready( function() {
  $("#plugin-content").load("plugins.html", function () {

    $('#plugin-table').DataTable ( {
      "lengthMenu": [[50, 100, -1], [50, 100, "All"]],
      "bAutoWidth": false,
      "dom": '<"pull-left"f><"pull-right"l>tip',
      "columnDefs": [ {
        "targets": 3,
        "orderable": false
      } ]
    });
  });
});
