ThisBuild / organization := "org.x86"
// Must match the Scala version used to publish the Chisel compiler plugin (3.5.6 -> 2.13.10).
ThisBuild / scalaVersion := "2.13.10"
ThisBuild / version      := "0.1.0"

val chiselVersion    = "3.5.6"
val chiselTestVersion = "0.5.6"

lazy val root = (project in file("."))
  .settings(
    name := "x86-core",
    libraryDependencies ++= Seq(
      "edu.berkeley.cs" %% "chisel3"    % chiselVersion,
      "edu.berkeley.cs" %% "chiseltest" % chiselTestVersion % Test
    ),
    addCompilerPlugin("edu.berkeley.cs" % "chisel3-plugin" % chiselVersion cross CrossVersion.full),
    scalacOptions ++= Seq(
      "-deprecation",
      "-feature",
      "-language:reflectiveCalls",
      "-Xcheckinit",
      "-Ymacro-annotations"
    ),
    Test / parallelExecution := false
  )
