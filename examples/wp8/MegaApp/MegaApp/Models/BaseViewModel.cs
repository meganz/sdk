using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MegaApp.Models
{
    public class BaseViewModel : INotifyPropertyChanged
    {
        #region Properties

        private bool _controlState;
        public bool ControlState
        {
            get { return _controlState; }
            set
            {
                _controlState = value;
                OnPropertyChanged("ControlState");
            }
        }

        #endregion

        #region INotifyPropertyChanged

        public event PropertyChangedEventHandler PropertyChanged;
        protected void OnPropertyChanged(string name)
        {
            var handler = PropertyChanged;
            if (handler != null)
            {
                handler(this, new PropertyChangedEventArgs(name));
            }
        }

        #endregion
    }
}
