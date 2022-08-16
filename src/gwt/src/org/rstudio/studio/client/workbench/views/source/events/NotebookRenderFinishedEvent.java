/*
 * NotebookRenderFinishedEvent.java
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
package org.rstudio.studio.client.workbench.views.source.events;

import org.rstudio.core.client.js.JavaScriptSerializable;
import org.rstudio.studio.client.application.events.CrossWindowEvent;

import com.google.gwt.event.shared.EventHandler;
import com.google.gwt.event.shared.GwtEvent;

@JavaScriptSerializable
public class NotebookRenderFinishedEvent 
             extends CrossWindowEvent<NotebookRenderFinishedEvent.Handler>
{
   public interface Handler extends EventHandler
   {
      void onNotebookRenderFinished(NotebookRenderFinishedEvent event);
   }

   public static final GwtEvent.Type<NotebookRenderFinishedEvent.Handler> TYPE = new GwtEvent.Type<>();
   
   public NotebookRenderFinishedEvent()
   {
   }

   public NotebookRenderFinishedEvent(String docId, String docPath)
   {
      docId_ = docId;
      docPath_ = docPath;
   }
   
   public String getDocId()
   {
      return docId_;
   }

   public String getDocPath()
   {
      return docPath_;
   }
   
   @Override
   protected void dispatch(NotebookRenderFinishedEvent.Handler handler)
   {
      handler.onNotebookRenderFinished(this);
   }

   @Override
   public GwtEvent.Type<NotebookRenderFinishedEvent.Handler> getAssociatedType()
   {
      return TYPE;
   }
   
   private String docId_;
   private String docPath_;
}
