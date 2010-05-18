==============================================
PostgreSQL Warm-Standby Replication with 2warm
==============================================

Introduction
============

PostgreSQL has shipped with an integrated standby features
since its version 8.2.  This allows creating a master/standby
pair of nodes, where the standby regularly receives a log of
database updates from the master.

While the main components of the feature are included with
PostgreSQL, the user is expected to provide many scripts to
handle tasks such as copying files between master and standby.

2warm makes this easy and robust.  It provides scripts to accomplish
all of the necessary steps in the most common configuration, where ssh
is used as the way to communicate between the master and standby.  It
also provides a set of management tools for easily setting up both
sides of a warm standby pair, and dealing with state transitions
as nodes are brought up, are taken down, or fail.

Documentation
=============

Full documentation is available in the docs/ subdirectory, written
in ReST markup.  Tools that operate on ReST can be used to make
versions of it formatted for other purposes, such as rst2html
to make a HTML version or rst2pdf for PDF.

Contact
=======

 * The project is hosted at http://projects.2ndQuadrant.com
 * Initial commits are done in the git reportiory at http://github.com/gregs1104/

If you have any hints, changes or improvements, please contact:

 * Greg Smith greg@2ndQuadrant.com

License
=======

2warm is licensed under a standard 3-clause BSD license.

Copyright (c) 2009-2010, 2ndQuadrant Limited
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
