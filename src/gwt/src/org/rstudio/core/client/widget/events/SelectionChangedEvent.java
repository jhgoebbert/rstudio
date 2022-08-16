/*
 * SelectionChangedEvent.java
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
package org.rstudio.core.client.widget.events;

import com.google.gwt.event.shared.EventHandler;
import com.google.gwt.event.shared.GwtEvent;

public class SelectionChangedEvent extends GwtEvent<SelectionChangedEvent.Handler>
{
   public static final Type<Handler> TYPE = new Type<>();

   public interface Handler extends EventHandler
   {
      void onSelectionChanged(SelectionChangedEvent e);
   }

   @Override
   public Type<Handler> getAssociatedType()
   {
      return TYPE;
   }

   @Override
   protected void dispatch(Handler handler)
   {
      handler.onSelectionChanged(this);
   }
}
