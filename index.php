<!DOCTYPE html>
<html>
<head>
    <title>User Data</title>
</head>
<body>

<?php
// Database connection details
$servername = "localhost";
$username = "abuzar"; // Use your actual database username
$password = "abuzar123";     // Use your actual database password
$dbname = "file_manager"; // Use your actual database name

// Create a connection
$conn = new mysqli($servername, $username, $password, $dbname);

// Check connection
if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

$sql = "SELECT id, name, email FROM users";
$result = $conn->query($sql);

if ($result->num_rows > 0) {
    echo "<h1>User Information</h1>";
    echo "<table border='1'>";
    echo "<tr><th>ID</th><th>Name</th><th>Email</th></tr>";
    // Output data of each row
    while($row = $result->fetch_assoc()) {
        echo "<tr>";
        echo "<td>" . $row["id"] . "</td>";
        echo "<td>" . $row["name"] . "</td>";
        echo "<td>" . $row["email"] . "</td>";
        echo "</tr>";
    }
    echo "</table>";
} else {
    echo "<h1>No users found.</h1>";
}

$conn->close();
?>

</body>
</html>