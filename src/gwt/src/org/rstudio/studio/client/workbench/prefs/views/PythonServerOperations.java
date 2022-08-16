/*
 * PythonServerOperations.java
 *
 * Copyright (C) 2022 by Posit, PBC
 *
 * Unless you have received this program directly from Posit pursuant
 * to the terms of a commercial license agreement with Posit, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */
package org.rstudio.studio.client.workbench.prefs.views;

import org.rstudio.studio.client.server.ServerRequestCallback;

public interface PythonServerOperations
{
   void pythonActiveInterpreter(ServerRequestCallback<PythonInterpreter> requestCallback);
   
   void pythonFindInterpreters(ServerRequestCallback<PythonInterpreters> requestCallback);
   
   void pythonInterpreterInfo(String interpreterPath,
                              ServerRequestCallback<PythonInterpreter> requestCallback);
}
